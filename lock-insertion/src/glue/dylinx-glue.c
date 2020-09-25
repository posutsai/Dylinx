// Include all of the lock definition in lock directory.
#include "lock/ttas-lock.h"
#include "lock/backoff-lock.h"
#include "lock/pthreadmtx-lock.h"
#include "dylinx-glue.h"
#include <errno.h>
#include <string.h>

#ifndef __DYLINX_GLUE__
#define __DYLINX_GLUE__
#pragma clang diagnostic ignored "-Waddress-of-packed-member"

#define COUNT_DOWN()                                                        \
  11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1

#define AVAILABLE_LOCK_TYPE_NUM(...)                                        \
  GET_MACRO(__VA_ARGS__)

#if AVAILABLE_LOCK_TYPE_NUM(ALLOWED_LOCK_TYPE, COUNT_DOWN()) > LOCK_TYPE_LIMIT
#error                                                                                                      \
Current number of available lock types isn't enough. Please reset                                           \
LOCK_TYPE_CNT macro and corresponding macro definition.
#endif

int dylinx_error_var_init(void *lock, const pthread_mutexattr_t *attr, char *file, char *var_name, int line) {
  char error_msg[1000];
  sprintf(
    error_msg,
    "Untrackable array of locks are trying to init. Possible"
    "cause is _Generic function falls into \'default\' option."
    "According to source code, it happens near %s L%d [%s]",
    file, line, var_name
  );
  HANDLING_ERROR(error_msg);
  return -1;
}

int dlx_untrack_var_init(dlx_generic_lock_t *lock, const pthread_mutexattr_t *attr, char *file, char *var_name, int line) {
#ifdef __DYLINX_DEBUG__
  char log_msg[300];
  printf("Untracked lock variable located in %s %s L%4d is initialized", file, var_name, line);
#endif
  if (lock->check_code == 0x32CB00B5);
    return 0;
  // The untracked lock instance is initialized with pthreadmtx
  // by default.
  lock->methods = calloc(1, sizeof(dlx_injected_interface_t));
  lock->methods->init_fptr = pthreadmtx_init;
  lock->methods->lock_fptr = pthreadmtx_lock;
  lock->methods->trylock_fptr = pthreadmtx_trylock;
  lock->methods->unlock_fptr = pthreadmtx_unlock;
  lock->methods->destroy_fptr = pthreadmtx_destroy;
  lock->methods->cond_timedwait_fptr = pthreadmtx_cond_timedwait;
  lock->check_code = 0x32CB00B5;
  return (!lock->methods || lock->methods->init_fptr(&lock->lock_obj, NULL))? -1: 0;
}

int dlx_error_arr_init(void *lock, uint32_t size, char *var_name, char *file, int line) {
  char error_msg[1000];
  sprintf(
    error_msg,
    "Untrackable array of locks are trying to init. Possible"
    "cause is _Generic function falls into \'default\' option."
    "According to source code, error of array %s happens near %s L%d",
    var_name, file, line
  );
  HANDLING_ERROR(error_msg);
  return -1;
}

int dlx_untrack_arr_init(dlx_generic_lock_t *lock, uint32_t num, char *var_name, char *file, int line) {
  for (uint32_t i = 0; i < num; i++) {
    if (lock[i].check_code == 0x32CB00B5)
      continue;

    // Certain lock element isn't initialized.
    lock[i].methods = calloc(1, sizeof(dlx_injected_interface_t));
    lock[i].methods->init_fptr = pthreadmtx_init;
    lock[i].methods->lock_fptr = pthreadmtx_lock;
    lock[i].methods->trylock_fptr = pthreadmtx_trylock;
    lock[i].methods->unlock_fptr = pthreadmtx_unlock;
    lock[i].methods->destroy_fptr = pthreadmtx_destroy;
    lock[i].methods->cond_timedwait_fptr = pthreadmtx_cond_timedwait;
    lock[i].check_code = 0x32CB00B5;

    if (!lock[i].methods || lock->methods->init_fptr(&lock->lock_obj, NULL))
      return -1;
  }
  return 0;
}

int dylinx_error_enable(void *lock) {
  HANDLING_ERROR(
    "Untrackable lock is trying to enable. Possible cause is"
    "_Generic function falls into \'default\' option."
  );
  return -1;
}

int dylinx_forward_enable(void *lock) {
  dlx_generic_lock_t *mtx = (dlx_generic_lock_t *)lock;
  return mtx->methods->lock_fptr(mtx->lock_obj);
}

int dylinx_error_disable(void *lock) {
  HANDLING_ERROR(
    "Untrackable lock is trying to unlock. Possible cause is"
    "_Generic function falls into \'default\' option."
  );
  return -1;
}

int dylinx_forward_disable(void *lock) {
  dlx_generic_lock_t *mtx = (dlx_generic_lock_t *)lock;
  return mtx->methods->unlock_fptr(mtx->lock_obj);
}

int dylinx_error_destroy(void *lock) {
  HANDLING_ERROR(
    "Untrackable lock is trying to destroy. Possible cause is"
    "_Generic function falls into \'default\' option."
  );
  return -1;
}

int dylinx_forward_destroy(void *lock) {
  dlx_generic_lock_t *mtx = (dlx_generic_lock_t *)lock;
  return mtx->methods->destroy_fptr(mtx->lock_obj);
}

int dylinx_error_cond_wait(pthread_cond_t *cond, void *lock) {
  HANDLING_ERROR(
    "Untrackable lock is trying to wait for condtion variable."
    "Possible cause is _Generic function falls into \'default\'"
    "option."
  );
  return -1;
}

int dylinx_forward_cond_wait(pthread_cond_t *cond, void *lock) {
  dlx_generic_lock_t *mtx = (dlx_generic_lock_t *)lock;
  return mtx->methods->cond_timedwait_fptr(cond, mtx->lock_obj, 0);
}

int dylinx_error_cond_timedwait(pthread_cond_t *cond, void *lock, const struct timespec *time) {
  HANDLING_ERROR(
    "Untrackable lock is trying to wait for condtion variable."
    "Possible cause is _Generic function falls into \'default\'"
    "option."
  );
  return -1;
}

int dylinx_forward_cond_timedwait(pthread_cond_t *cond, void *lock, const struct timespec *time) {
  dlx_generic_lock_t *mtx = (dlx_generic_lock_t *)lock;
  return mtx->methods->cond_timedwait_fptr(cond, mtx->lock_obj, time);
}

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

#define DLX_IMPLEMENT_EACH_LOCK(...) FOR_EACH(DLX_LOCK_TEMPLATE_IMPLEMENT, __VA_ARGS__)
DLX_IMPLEMENT_EACH_LOCK(ALLOWED_LOCK_TYPE)

#endif // __DYLINX_GLUE__
