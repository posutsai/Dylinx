#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "dylinx-runtime-config.h"
// Include all of the lock definition in lock directory.
#include "lock/ttas-lock.h"
#include "lock/backoff-lock.h"

#ifndef __DYLINX_SYMBOL__
#define __DYLINX_SYMBOL__

#define dylinx_init_func(ltype) dylinx_ ## ltype ## lock_t: dylinx_ ## ltype ## lock_init,
#define DYLINX_INIT_LOCK_LIST(...) FOR_EACH(dylinx_init_func, __VA_ARGS__)
#define dylinx_enable_func(ltype) dylinx_ ## ltype ## lock_t: dylinx_ ## ltype ## lock_enable,
#define DYLINX_ENABLE_LOCK_LIST(...) FOR_EACH(dylinx_enable_func, __VA_ARGS__)
#define dylinx_cast_func(ltype) dylinx_ ## ltype ## lock_t: dylinx_ ## ltype ## lock_cast,
#define DYLINX_ENABLE_LOCK_LIST(...) FOR_EACH(dylinx_cast_func, __VA_ARGS__)

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
  dylinx_backofflock_t *: dylinx_backoff_fill_array,                        \
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
