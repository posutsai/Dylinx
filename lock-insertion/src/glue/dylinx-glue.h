#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "dylinx-runtime-config.h"

#ifndef __DYLINX_SYMBOL__
#define __DYLINX_SYMBOL__
#pragma clang diagnostic ignored "-Wincompatible-pointer-types"

#define ALLOWED_LOCK_TYPE pthreadmtx, ttas, backoff

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

typedef struct __attribute__((packed)) GenericInterface {
  void *entity;
  int (*initializer)(void *, const pthread_mutexattr_t *);
  int (*locker)(void *);
  int (*unlocker)(void *);
  int (*finalizer)(void *);
  int (*cond_waiter)(pthread_cond_t *, void *);
} generic_interface_t;

#define DYLINX_EXTERIOR_WRAPPER_PROTO(ltype)                                                                  \
  typedef union Dylinx ## ltype ## Lock {                                                                     \
    pthread_mutex_t dummy_lock;                                                                               \
    generic_interface_t interface;                                                                            \
  } dylinx_ ## ltype ## lock_t;                                                                               \
  int dylinx_ ## ltype ## lock_init(dylinx_ ## ltype ## lock_t *, pthread_mutexattr_t *);

DYLINX_EXTERIOR_WRAPPER_PROTO(ttas)
DYLINX_EXTERIOR_WRAPPER_PROTO(backoff);
DYLINX_EXTERIOR_WRAPPER_PROTO(pthreadmtx);

int dylinx_error_init(generic_interface_t *);
int dylinx_error_disable(generic_interface_t *);
int dylinx_error_destroy(generic_interface_t *lock);
int dylinx_error_condwait(pthread_cond_t *cond, generic_interface_t *mtx);
int dylinx_forward_enable(generic_interface_t *gen_lock);
int dylinx_forward_disable(generic_interface_t *gen_lock);
int dylinx_forward_destroy(generic_interface_t *gen_lock);

//  Since we force mutex to get initialized when the memory is allocated,
//  there is no chance that initialization encounters typeless lock. However,
//  things are different in other interfaces. When encoutering acceptable
//  types argument, it naively bypass the lock and conduct corresponding ops.
//  The reason why we use _Generic function here is to raise error while inputing
//  unacceptable argument.
#define dylinx_init_func(ltype) dylinx_ ## ltype ## lock_t *: dylinx_ ## ltype ## lock_init,
#define DYLINX_INIT_LOCK_LIST(...) FOR_EACH(dylinx_init_func, __VA_ARGS__)
#define __dylinx_member_init_(entity, attr) _Generic((entity),              \
  DYLINX_INIT_LOCK_LIST(ALLOWED_LOCK_TYPE)                                  \
  default: dylinx_error_init                                                \
)(entity, attr)

#define dylinx_enable_func(ltype) dylinx_ ## ltype ## lock_t *: dylinx_forward_enable,
#define DYLINX_ENABLE_LOCK_LIST(...) FOR_EACH(dylinx_enable_func, __VA_ARGS__)
#define __dylinx_generic_enable_(entity) _Generic((entity),                 \
  DYLINX_ENABLE_LOCK_LIST(ALLOWED_LOCK_TYPE)                                \
  generic_interface_t *: dylinx_forward_enable,                             \
  default: dylinx_error_enable                                              \
)(entity)

#define dylinx_disable_func(ltype) dylinx_ ## ltype ## lock_t *: dylinx_forward_disable,
#define DYLINX_DISABLE_LOCK_LIST(...) FOR_EACH(dylinx_disable_func, __VA_ARGS__)
#define __dylinx_generic_disable_(entity) _Generic((entity),                \
  DYLINX_DISABLE_LOCK_LIST(ALLOWED_LOCK_TYPE)                               \
  generic_interface_t *: dylinx_forward_disable,                            \
  default: dylinx_error_disable                                             \
)(entity)

#define dylinx_destroy_func(ltype) dylinx_ ## ltype ## lock_t *: dylinx_forward_destroy,
#define DYLINX_DESTROY_LOCK_LIST(...) FOR_EACH(dylinx_destroy_func, __VA_ARGS__)
#define __dylinx_generic_destroy_(entity) _Generic((entity),                \
  DYLINX_DESTROY_LOCK_LIST(ALLOWED_LOCK_TYPE)                               \
  generic_interface_t *: dylinx_forward_destroy,                            \
  default: dylinx_error_destroy                                             \
)(entity)

#define dylinx_condwait_func(ltype) dylinx_ ## ltype ## lock_t *: dylinx_forward_condwait,
#define DYLINX_CONDWAIT_LOCK_LIST(...) FOR_EACH(dylinx_condwait_func, __VA_ARGS__)
#define __dylinx_generic_condwait_(cond, mtx) _Generic((mtx),               \
  DYLINX_CONDWAIT_LOCK_LIST(ALLOWED_LOCK_TYPE)                              \
  generic_interface_t *: dylinx_forward_condwait,                           \
  default: dylinx_error_condwait                                            \
)(cond, mtx)

#endif // __DYLINX_SYMBOL__
