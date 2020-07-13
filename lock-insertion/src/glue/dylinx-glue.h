#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "dylinx-runtime-config.h"

#ifndef __DYLINX_SYMBOL__
#define __DYLINX_SYMBOL__
#pragma clang diagnostic ignored "-Wincompatible-pointer-types"

#define FE_0(WHAT)
#define FE_1(WHAT, X) WHAT(X)
#define FE_2(WHAT, X, ...) WHAT(X)FE_1(WHAT, __VA_ARGS__)
#define FE_3(WHAT, X, ...) WHAT(X)FE_2(WHAT, __VA_ARGS__)
#define FE_4(WHAT, X, ...) WHAT(X)FE_3(WHAT, __VA_ARGS__)
#define FE_5(WHAT, X, ...) WHAT(X)FE_4(WHAT, __VA_ARGS__)
#define FE_6(WHAT, X, ...) WHAT(X)FE_5(WHAT, __VA_ARGS__)
#define FE_7(WHAT, X, ...) WHAT(X)FE_6(WHAT, __VA_ARGS__)
#define FE_8(WHAT, X, ...) WHAT(X)FE_7(WHAT, __VA_ARGS__)
#define FE_9(WHAT, X, ...) WHAT(X)FE_8(WHAT, __VA_ARGS__)
#define FE_10(WHAT, X, ...) WHAT(X)FE_9(WHAT, __VA_ARGS__)

#define GET_MACRO(_0,_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,NAME,...) NAME
#define FOR_EACH(action,...)                                                \
  GET_MACRO(                                                                \
      _0,__VA_ARGS__,FE_10,FE_9,FE_8,FE_7,FE_6,FE_5,FE_4,FE_3,              \
      FE_2,FE_1,FE_0,                                                       \
  )(action,__VA_ARGS__)

#define ALLOWED_LOCK_TYPE pthreadmtx, ttas, backoff

typedef struct __attribute__((packed)) GenericInterface {
  void *entity;
  // dylinx_type has the same offset as __owners in
  // pthread_mutex_t.
  int32_t dylinx_type;
  struct Methods4Lock *methods;
  pthread_mutex_t *cv_mtx;
  char padding[sizeof(pthread_mutex_t) - 28];
} generic_interface_t;

// In order to add your customized lock and integrate it with
// Dylinx mechanism, there are two macros worth to be mentioned.
// The function of macro LOCK_DEFINE defined here is to specify
// correspoinding function prototype. On the other hand, the
// macro DYLINX_INIT_LOCK which is defined in utils.h is used to
// provide actual offer corresponding function content and assign
// the lock an unique id.

#define DYLINX_EXTERIOR_WRAPPER_PROTO(ltype)                                                                                    \
  typedef union Dylinx ## ltype ## Lock {                                                                     \
    pthread_mutex_t dummy_lock;                                                                               \
    generic_interface_t interface;                                                                            \
  } dylinx_ ## ltype ## lock_t;                                                                               \
  generic_interface_t *dylinx_ ## ltype ## lock_cast(dylinx_ ## ltype ## lock_t *lock);                       \
  void dylinx_ ## ltype ## lock_fill_array(dylinx_ ## ltype ## lock_t *head, size_t len);                     \
  int dylinx_ ## ltype ## lock_init(dylinx_ ## ltype ## lock_t *lock, pthread_mutexattr_t *attr);             \
  int dylinx_ ## ltype ## lock_enable(dylinx_ ## ltype ## lock_t *lock);                                      \

#define dylinx_init_func(ltype) dylinx_ ## ltype ## lock_t *: dylinx_ ## ltype ## lock_init,
#define DYLINX_INIT_LOCK_LIST(...) FOR_EACH(dylinx_init_func, __VA_ARGS__)
#define dylinx_enable_func(ltype) dylinx_ ## ltype ## lock_t *: dylinx_ ## ltype ## lock_enable,
#define DYLINX_ENABLE_LOCK_LIST(...) FOR_EACH(dylinx_enable_func, __VA_ARGS__)
#define dylinx_cast_func(ltype) dylinx_ ## ltype ## lock_t *: dylinx_ ## ltype ## lock_cast,
#define DYLINX_CAST_LOCK_LIST(...) FOR_EACH(dylinx_cast_func, __VA_ARGS__)
#define dylinx_union_proto(ltype) typedef union Dylinx ## ltype ## Lock dylinx_ ## ltype ## lock_t;
#define DYLINX_UNION_PROTO_LIST(...) FOR_EACH(dylinx_union_proto, __VA_ARGS__)

DYLINX_EXTERIOR_WRAPPER_PROTO(ttas)
DYLINX_EXTERIOR_WRAPPER_PROTO(backoff);
DYLINX_EXTERIOR_WRAPPER_PROTO(pthreadmtx);

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
// DYLINX_UNION_PROTO_LIST(ALLOWED_LOCK_TYPE)

#define __dylinx_generic_fill_array_(entity, bytes) _Generic((entity),      \
  pthread_mutex_t *: dummy_func,                                            \
  dylinx_pthreadmtxlock_t *: dylinx_pthreadmtxlock_fill_array,              \
  dylinx_ttaslock_t *: dylinx_ttaslock_fill_array,                          \
  dylinx_backofflock_t *: dylinx_backofflock_fill_array,                    \
  default: dylinx_degenerate_fill_array                                     \
)(entity, bytes)

#define FILL_ARRAY(head, bytes)                                             \
  do {                                                                      \
    __dylinx_generic_fill_array_(head, (bytes / sizeof(pthread_mutex_t)));  \
  } while(0)

// dylinx_genlock_forward is only apply when the lock instance is passed nestedly
// in interior scope which suppose to have generic_interface_t type.
#define __dylinx_generic_cast_(entity) _Generic((entity),                   \
  pthread_mutex_t *: native_pthreadmtx_forward,                             \
  DYLINX_CAST_LOCK_LIST(ALLOWED_LOCK_TYPE)                                  \
  default: dylinx_genlock_forward                                           \
)(entity)

#define __dylinx_generic_init_(entity, attr) _Generic((entity),             \
  pthread_mutex_t *: pthread_mutex_init,                                    \
  DYLINX_INIT_LOCK_LIST(ALLOWED_LOCK_TYPE)                                  \
  generic_interface_t *: dylinx_typeless_init                               \
)(entity, attr)

#define __dylinx_generic_enable_(entity) _Generic((entity),                 \
  pthread_mutex_t *: pthread_mutex_lock,                                    \
  DYLINX_ENABLE_LOCK_LIST(ALLOWED_LOCK_TYPE)                                \
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
