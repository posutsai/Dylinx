#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#ifndef __DYLINX_REPLACE_PTHREAD_NATIVE__
#define __DYLINX_REPLACE_PTHREAD_NATIVE__
#define pthread_mutex_init pthread_mutex_init_original
#define pthread_mutex_lock pthread_mutex_lock_original
#define pthread_mutex_unlock pthread_mutex_unlock_original
#define pthread_mutex_destroy pthread_mutex_destroy_original
#define pthread_mutex_trylock pthread_mutex_trylock_original
#define pthread_cond_wait pthread_cond_wait_original
#define pthread_cond_timedwait pthread_cond_timedwait_original
#include <pthread.h>
#undef PTHREAD_MUTEX_INITIALIZER
#define PTHREAD_MUTEX_INITIALIZER {NULL, 0, {0XABADBABE, 0xFEE1DEAD}, NULL, {0}}
#undef pthread_mutex_init
#undef pthread_mutex_lock
#undef pthread_mutex_unlock
#undef pthread_mutex_destroy
#undef pthread_mutex_trylock
#undef pthread_cond_wait
#undef pthread_cond_timedwait
#endif


#ifndef __DYLINX_SYMBOL__
#define __DYLINX_SYMBOL__
#pragma clang diagnostic ignored "-Wincompatible-pointer-types"
#pragma clang diagnostic ignored "-Wmacro-redefined"

#define XRAY_ATTR                                                                 \
  __attribute__((xray_always_instrument))                                         \
  __attribute__((xray_log_args(2)))
#define ALLOWED_LOCK_TYPE pthreadmtx, ttas, backoff, adaptivemtx, mcs
#define LOCK_TYPE_LIMIT 10
#define DYLINX_LOCK_TO_TYPE(lock) dlx_ ## lock ## _t
#define DYLINX_LOCK_TO_INIT_METHOD

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

typedef struct InjectedInterfaces {
  int (*init_fptr)(void **, pthread_mutexattr_t *);
  int (*lock_fptr)(void *);
  int (*trylock_fptr)(void *);
  int (*unlock_fptr)(void *);
  int (*destroy_fptr)(void *);
  int (*cond_timedwait_fptr)(pthread_cond_t *, void *, const struct timespec *);
} dlx_injected_interface_t;

typedef union {
  struct { int32_t type_id; uint32_t ins_id; } pair;
  int64_t long_id;
} indicator_t;

typedef struct __attribute__((packed)) GenericLock {
  // Point to actual memory resource for future usage.
  void *lock_obj;
  // check_code is used as telling whether the specific
  // instance is already initialized or not. If it is
  // already initialized, it should be 0x32CB00B5. The
  // member value should also be modified in destroy
  // function since os may reuse the memory.
  uint32_t check_code;
  indicator_t ind;
  dlx_injected_interface_t *methods;
  char padding[sizeof(pthread_mutex_t) - 3 * sizeof(uint32_t) - 2 * sizeof(void *)];
} dlx_generic_lock_t;

static int (*native_mutex_init)(pthread_mutex_t *, pthread_mutexattr_t *);
static int (*native_mutex_lock)(pthread_mutex_t *);
static int (*native_mutex_unlock)(pthread_mutex_t *);
static int (*native_mutex_destroy)(pthread_mutex_t *);
static int (*native_mutex_trylock)(pthread_mutex_t *);
static int (*native_cond_wait)(pthread_cond_t *, pthread_mutex_t *);
static int (*native_cond_timedwait)(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);

#define DLX_LOCK_TEMPLATE_PROTOTYPE(ltype)                                                                     \
  typedef union Dylinx ## ltype ## Lock {                                                                      \
    dlx_generic_lock_t interface;                                                                              \
    pthread_mutex_t dummy_lock;                                                                                \
  } dlx_ ## ltype ## _t;                                                                                       \
  int dlx_ ## ltype ## _var_init(                                                                              \
    dlx_ ## ltype ## _t *,                                                                                     \
    pthread_mutexattr_t *,                                                                                     \
    int32_t,                                                                                                   \
    char *var_name, char *file, int line                                                                       \
  );                                                                                                           \
  int dlx_ ## ltype ## _check_init(                                                                            \
    dlx_ ## ltype ## _t *,                                                                                     \
    pthread_mutexattr_t *,                                                                                     \
    char *var_name, char *file, int line                                                                       \
  );                                                                                                           \
  int dlx_ ## ltype ## _arr_init(                                                                              \
    dlx_ ## ltype ## _t *,                                                                                     \
    uint32_t,                                                                                                  \
    int32_t type_id,                                                                                           \
    char *var_name, char *file, int line                                                                       \
  );                                                                                                           \
  void *dlx_ ## ltype ## _obj_init(                                                                            \
    uint32_t cnt,                                                                                              \
    uint32_t unit,                                                                                             \
    uint32_t *offsets,                                                                                         \
    uint32_t n_offset,                                                                                         \
    void ** init_funcs,                                                                                        \
    int32_t *type_ids,                                                                                         \
    char *file,                                                                                                \
    int line                                                                                                   \
  );                                                                                                           \
  int ltype ## _init(void **, pthread_mutexattr_t *);                                                          \
  int ltype ## _lock(void *);                                                                                  \
  int ltype ## _trylock(void *);                                                                               \
  int ltype ## _unlock(void *);                                                                                \
  int ltype ## _destroy(void *);                                                                               \
  int ltype ## _cond_timedwait(pthread_cond_t *, void *, const struct timespec *);                             \
  extern const dlx_injected_interface_t dlx_ ## ltype ## _methods_collection;

#define DLX_LOCK_TEMPLATE_PROTOTYPE_LIST(...) FOR_EACH(DLX_LOCK_TEMPLATE_PROTOTYPE, __VA_ARGS__)
DLX_LOCK_TEMPLATE_PROTOTYPE_LIST(ALLOWED_LOCK_TYPE)

// For debugging and tracking purpose, we add the last three function argument.
int dlx_untrack_var_init(dlx_generic_lock_t *, const pthread_mutexattr_t *, int type_id, char *var_name, char *file, int line);
int dlx_untrack_check_init(dlx_generic_lock_t *, const pthread_mutexattr_t *, char *var_name, char *file, int line);
int dlx_untrack_arr_init(dlx_generic_lock_t *, uint32_t, int type_id, char *var_name, char *file, int line);
int dlx_error_var_init(void *, const pthread_mutexattr_t *, int type_id, char *var_name, char *file, int line);
int dlx_error_check_init(void *, const pthread_mutexattr_t *, char *var_name, char *file, int line);
int dlx_error_arr_init(void *, uint32_t, int type_id, char *var_name, char *file, int line);
void *dlx_error_obj_init(uint32_t, uint32_t, uint32_t *, uint32_t, void **, int *type_ids, char *, int);
void *dlx_struct_obj_init(uint32_t, uint32_t, uint32_t *, uint32_t, void **, int *type_ids, char *, int);

XRAY_ATTR int dlx_error_enable(int64_t, void *, char *, char *, int);
XRAY_ATTR int dlx_error_disable(int64_t, void *, char *, char *, int);
XRAY_ATTR int dlx_error_destroy(int64_t, void *);
XRAY_ATTR int dlx_error_trylock(int64_t, void *, char *, char *, int);
int dlx_error_cond_timedwait(pthread_cond_t *, void *, const struct timespec *);
int dlx_error_cond_wait(pthread_cond_t *, void *);
XRAY_ATTR int dlx_forward_enable(int64_t, void *, char *, char *, int);
XRAY_ATTR int dlx_forward_disable(int64_t, void *, char *, char *, int);
XRAY_ATTR int dlx_forward_destroy(int64_t, void *);
XRAY_ATTR int dlx_forward_trylock(int64_t, void *, char *, char *, int);
int dlx_forward_cond_wait(pthread_cond_t *, void *);
int dlx_forward_cond_timedwait(pthread_cond_t *, void *, const struct timespec *);

typedef struct UserDefStruct {
  void *dummy;
} user_def_struct_t;

extern uint32_t g_ins_id;

//  Direct call by user
//  ----------------------------------------------------------------------------
//  Since we force mutex to get initialized when the memory is allocated,
//  there is no chance that initialization encounters typeless lock. However,
//  things are different in other interfaces. When encoutering acceptable
//  types argument, it naively bypass the lock and conduct corresponding ops.
//  The reason why we use _Generic function here is to raise error while inputing
//  unacceptable argument.
#define DLX_GENERIC_VAR_INIT_TYPE_REDIRECT(ltype) dlx_ ## ltype ## _t *: dlx_ ## ltype ## _var_init,
#define DLX_GENERIC_VAR_INIT_TYPE_LIST(...) FOR_EACH(DLX_GENERIC_VAR_INIT_TYPE_REDIRECT, __VA_ARGS__)
#define __dylinx_member_init_(entity, attr, type_id) _Generic((entity),                                        \
  DLX_GENERIC_VAR_INIT_TYPE_LIST(ALLOWED_LOCK_TYPE)                                                            \
  dlx_generic_lock_t *: dlx_untrack_var_init,                                                                  \
  default: dlx_error_var_init                                                                                  \
)(entity, attr, type_id, #entity, __FILE__, __LINE__)

#define DLX_GENERIC_CHECK_INIT_TYPE_REDIRECT(ltype) dlx_ ## ltype ## _t *: dlx_ ## ltype ## _check_init,
#define DLX_GENERIC_CHECK_INIT_TYPE_LIST(...) FOR_EACH(DLX_GENERIC_CHECK_INIT_TYPE_REDIRECT, __VA_ARGS__)
#define pthread_mutex_init(entity, attr) _Generic((entity),                                                    \
  DLX_GENERIC_CHECK_INIT_TYPE_LIST(ALLOWED_LOCK_TYPE)                                                          \
  dlx_generic_lock_t *: dlx_untrack_check_init,                                                                \
  default: dlx_error_check_init                                                                                  \
)(entity, attr, #entity, __FILE__, __LINE__)

#define DLX_GENERIC_OBJ_INIT_TYPE_REDIRECT(ltype) dlx_ ## ltype ## _t *: dlx_ ## ltype ## _obj_init,
#define DLX_GENERIC_OBJ_INIT_TYPE_LIST(...) FOR_EACH(DLX_GENERIC_OBJ_INIT_TYPE_REDIRECT, __VA_ARGS__)
#define __dylinx_object_init_(cnt, unit, offsets, n_offset, ltype, init_funcs, type_ids) _Generic((ltype),     \
  DLX_GENERIC_OBJ_INIT_TYPE_LIST(ALLOWED_LOCK_TYPE)                                                            \
  user_def_struct_t *: dlx_struct_obj_init,                                                                    \
  default: dlx_error_obj_init                                                                                  \
)(cnt, unit, offsets, n_offset, init_funcs, type_ids,  __FILE__, __LINE__)

#define DLX_GENERIC_ARR_INIT_TYPE_REDIRECT(ltype) dlx_ ## ltype ## _t *: dlx_ ## ltype ## _arr_init,
#define DLX_GENERIC_ARR_INIT_TYPE_LIST(...) FOR_EACH(DLX_GENERIC_ARR_INIT_TYPE_REDIRECT, __VA_ARGS__)
#define __dylinx_array_init_(entity, len, type_id) _Generic((entity),                                          \
  DLX_GENERIC_ARR_INIT_TYPE_LIST(ALLOWED_LOCK_TYPE)                                                            \
  dlx_generic_lock_t *: dlx_untrack_arr_init,                                                                  \
  default: dlx_error_arr_init                                                                                  \
)(entity, len, type_id, #entity, __FILE__, __LINE__)

#define DLX_GENERIC_ENABLE_TYPE_REDIRECT(ltype) dlx_ ## ltype ## _t *: dlx_forward_enable,
#define DLX_GENERIC_ENABLE_TYPE_LIST(...) FOR_EACH(DLX_GENERIC_ENABLE_TYPE_REDIRECT, __VA_ARGS__)
#define pthread_mutex_lock(entity) _Generic((entity),                                                         \
  DLX_GENERIC_ENABLE_TYPE_LIST(ALLOWED_LOCK_TYPE)                                                             \
  dlx_generic_lock_t *: dlx_forward_enable,                                                                   \
  default: dlx_error_enable                                                                                   \
)((entity)->interface.ind.long_id, entity, #entity, __FILE__, __LINE__)

#define DLX_GENERIC_DISABLE_TYPE_REDIRECT(ltype) dlx_ ## ltype ## _t *: dlx_forward_disable,
#define DLX_GENERIC_DISABLE_TYPE_LIST(...) FOR_EACH(DLX_GENERIC_DISABLE_TYPE_REDIRECT, __VA_ARGS__)
#define pthread_mutex_unlock(entity) _Generic((entity),                                                       \
  DLX_GENERIC_DISABLE_TYPE_LIST(ALLOWED_LOCK_TYPE)                                                            \
  dlx_generic_lock_t *: dlx_forward_disable,                                                                  \
  default: dlx_error_disable                                                                                  \
)((entity)->interface.ind.long_id, entity, #entity, __FILE__, __LINE__)

#define DLX_GENERIC_DESTROY_TYPE_REDIRECT(ltype) dlx_ ## ltype ## _t *: dlx_forward_destroy,
#define DLX_GENERIC_DESTROY_TYPE_LIST(...) FOR_EACH(DLX_GENERIC_DESTROY_TYPE_REDIRECT, __VA_ARGS__)
#define pthread_mutex_destroy(entity) _Generic((entity),                                                      \
  DLX_GENERIC_DESTROY_TYPE_LIST(ALLOWED_LOCK_TYPE)                                                            \
  dlx_generic_lock_t *: dlx_forward_destroy,                                                                  \
  default: dlx_error_destroy                                                                                  \
)((entity)->interface.ind.long_id, entity)

#define DLX_GENERIC_TRYLOCK_TYPE_REDIRECT(ltype) dlx_ ## ltype ## _t *: dlx_forward_trylock,
#define DLX_GENERIC_TRYLOCK_TYPE_LIST(...) FOR_EACH(DLX_GENERIC_TRYLOCK_TYPE_REDIRECT, __VA_ARGS__)
#define pthread_mutex_trylock(entity) _Generic((entity),                                                     \
  DLX_GENERIC_TRYLOCK_TYPE_LIST(ALLOWED_LOCK_TYPE)                                                           \
  dlx_generic_lock_t *: dlx_forward_trylock,                                                                 \
  default: dlx_error_trylock                                                                                 \
)((entity)->interface.ind.long_id, entity, #entity, __FILE__, __LINE__)

#define DLX_GENERIC_COND_WAIT_TYPE_REDIRECT(ltype) dlx_ ## ltype ## _t *: dlx_forward_cond_wait,
#define DLX_GENERIC_COND_WAIT_TYPE_LIST(...) FOR_EACH(DLX_GENERIC_COND_WAIT_TYPE_REDIRECT, __VA_ARGS__)
#define pthread_cond_wait(cond, mtx) _Generic((mtx),                                                         \
  DLX_GENERIC_COND_WAIT_TYPE_LIST(ALLOWED_LOCK_TYPE)                                                         \
  dlx_generic_lock_t *: dlx_forward_cond_wait,                                                               \
  default: dlx_error_cond_wait                                                                               \
)(cond, mtx)

#define DLX_GENERIC_COND_TIMEDWAIT_TYPE_REDIRECT(ltype) dlx_ ## ltype ## _t *: dlx_forward_cond_timedwait,
#define DLX_GENERIC_COND_TIMEDWAIT_TYPE_LIST(...) FOR_EACH(DLX_GENERIC_COND_TIMEDWAIT_TYPE_REDIRECT, __VA_ARGS__)
#define pthread_cond_timedwait(cond, mtx, time) _Generic((mtx),                                             \
  DLX_GENERIC_COND_TIMEDWAIT_TYPE_LIST(ALLOWED_LOCK_TYPE)                                                   \
  dlx_generic_lock_t *: dlx_forward_cond_timedwait,                                                         \
  default: dlx_error_cond_timedwait                                                                         \
)(cond, mtx, time)

#endif // __DYLINX_SYMBOL__
