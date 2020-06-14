#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "dylinx-runtime-config.h"

#ifndef __DYLINX_SYMBOL__
#define __DYLINX_SYMBOL__

#define LOCK_DEFINE(ltype)                                                                                    \
  typedef union Dylinx ## ltype ## Lock {                                                                     \
    pthread_mutex_t dummy_lock;                                                                               \
    generic_interface_t interface;                                                                            \
  } dylinx_ ## ltype ## lock_t;                                                                               \
  generic_interface_t *dylinx_ ## ltype ## lock_cast(dylinx_ ## ltype ## lock_t *lock);                       \
  void dylinx_ ## ltype ## lock_fill_array(dylinx_ ## ltype ## lock_t *head, size_t len);                     \
  int dylinx_ ## ltype ## lock_init(dylinx_ ## ltype ## lock_t *lock, pthread_mutexattr_t *attr);             \
  int dylinx_ ## ltype ## lock_enable(dylinx_ ## ltype ## lock_t *lock);                                      \

typedef struct __attribute__((packed)) GenericInterface {
  void *entity;
  // dylinx_type has the same offset as __owners in
  // pthread_mutex_t.
  int32_t dylinx_type;
  struct Methods4Lock *methods;
  pthread_mutex_t *cv_mtx;
  char padding[sizeof(pthread_mutex_t) - 28];
} generic_interface_t;

LOCK_DEFINE(ttas);
LOCK_DEFINE(pthreadmtx);

int dylinx_lock_disable(void *lock);
int dylinx_lock_destroy(void *lock);
int dylinx_lock_condwait(pthread_cond_t *cond, void *mtx);
generic_interface_t *dylinx_genlock_forward(generic_interface_t *gen_lock);
int dylinx_typeless_init(generic_interface_t *genlock, pthread_mutexattr_t *attr);
int dylinx_typeless_enable(generic_interface_t *gen_lock);
void dummy_func(pthread_mutex_t *mtx, size_t len);
void dylinx_degenerate_fill_array(generic_interface_t *mtx, size_t len);
int dylinx_typeless_destroy(generic_interface_t *gen_lock);
generic_interface_t *native_pthreadmtx_forward(pthread_mutex_t *mtx);

#define __dylinx_generic_fill_array_(entity, bytes) _Generic((entity),      \
  pthread_mutex_t *: dummy_func,                                            \
  dylinx_pthreadmtxlock_t *: dylinx_pthreadmtxlock_fill_array,              \
  dylinx_ttaslock_t *: dylinx_ttaslock_fill_array,                          \
  default: dylinx_degenerate_fill_array                                     \
)(entity, bytes)

#define FILL_ARRAY(head, bytes)                                             \
  do {                                                                      \
    __dylinx_generic_fill_array_(head, (bytes / sizeof(pthread_mutex_t)));  \
  } while(0)

// dylinx_genlock_forward is only apply when the lock instance is passed nestedly
// in interior scope.
#define __dylinx_generic_cast_(entity) _Generic((entity),                   \
  pthread_mutex_t *: native_pthreadmtx_forward,                             \
  dylinx_pthreadmtxlock_t *: dylinx_pthreadmtxlock_cast,                    \
  dylinx_ttaslock_t *: dylinx_ttaslock_cast,                                \
  default: dylinx_genlock_forward                                           \
)(entity)

#define __dylinx_generic_init_(entity, attr) _Generic((entity),             \
  pthread_mutex_t *: pthread_mutex_init,                                    \
  dylinx_pthreadmtxlock_t *: dylinx_pthreadmtxlock_init,                    \
  dylinx_ttaslock_t *: dylinx_ttaslock_init,                                \
  generic_interface_t *: dylinx_typeless_init                               \
)(entity, attr)

#define __dylinx_generic_enable_(entity) _Generic((entity),                 \
  pthread_mutex_t *: pthread_mutex_lock,                                    \
  dylinx_pthreadmtxlock_t *: dylinx_pthreadmtxlock_enable,                  \
  dylinx_ttaslock_t *: dylinx_ttaslock_enable,                              \
  generic_interface_t *: dylinx_typeless_enable                             \
)(entity)

#define __dylinx_generic_disable_(entity) _Generic((entity),                \
  pthread_mutex_t *: pthread_mutex_unlock,                                  \
  default: dylinx_lock_disable                                              \
)(entity)

#define __dylinx_generic_destroy_(entity) _Generic((entity),                \
  pthread_mutex_t *: pthread_mutex_destroy,                                 \
  default: dylinx_lock_destroy                                              \
)(entity)


#define __dylinx_generic_condwait_(cond, mtx) _Generic((mtx),               \
  pthread_mutex_t *: pthread_cond_wait,                                     \
  default: dylinx_lock_condwait                                             \
)(cond, mtx)

#endif // __DYLINX_SYMBOL__
