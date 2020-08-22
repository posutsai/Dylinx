#include "dylinx-glue.h"
// Include all of the lock definition in lock directory.
#include "lock/ttas-lock.h"
#include "lock/backoff-lock.h"
#include <errno.h>
#include <string.h>

#ifndef __DYLINX_GLUE__
#define __DYLINX_GLUE__
#define DYLINX_PTHREADMTX_ID 1
#pragma clang diagnostic ignored "-Waddress-of-packed-member"

#define COUNT_DOWN()                                                        \
  11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1

#define AVAILABLE_LOCK_TYPE_NUM(...)                                        \
  GET_MACRO(__VA_ARGS__)

#if AVAILABLE_LOCK_TYPE_NUM(ALLOWED_LOCK_TYPE, COUNT_DOWN()) > LOCK_TYPE_CNT
#error                                                                      \
Current number of available lock types isn't enough. Please reset           \
LOCK_TYPE_CNT macro and corresponding macro definition.
#endif

int dylinx_error_init(generic_interface_t *lock) {
  HANDLING_ERROR(
    "Untrackable lock is trying to init. Possible cause is"
    "_Generic function falls into \'default\' option."
  );
  return -1;
}

// {{{ enable function pair
int dylinx_error_enable(generic_interface_t *lock) {
  HANDLING_ERROR(
    "Untrackable lock is trying to enable. Possible cause is"
    "_Generic function falls into \'default\' option."
  );
  return -1;
}

int dylinx_forward_enable(generic_interface_t *lock) {
  generic_interface_t *mtx = (generic_interface_t *)lock;
  return mtx->locker(mtx->entity);
}
// }}}

// {{{ disable function pair
int dylinx_error_disable(generic_interface_t *lock) {
  HANDLING_ERROR(
    "Untrackable lock is trying to unlock. Possible cause is"
    "_Generic function falls into \'default\' option."
  );
  return -1;
}

int dylinx_forward_disable(generic_interface_t *lock) {
  generic_interface_t *mtx = (generic_interface_t *)lock;
  return mtx->unlocker(mtx->entity);
}
// }}}

// {{{ destroy function pair
int dylinx_error_destroy(generic_interface_t *lock) {
  HANDLING_ERROR(
    "Untrackable lock is trying to destroy. Possible cause is"
    "_Generic function falls into \'default\' option."
  );
  return -1;
}

int dylinx_forward_destroy(generic_interface_t *lock) {
  generic_interface_t *mtx = (generic_interface_t *)lock;
  return mtx->finalizer(mtx->entity);
}
// }}}

// {{{ condwait function pair
int dylinx_error_condwait(pthread_cond_t *cond, generic_interface_t *lock) {
  HANDLING_ERROR(
    "Untrackable lock is trying to wait for condtion variable."
    "Possible cause is _Generic function falls into \'default\'"
    "option."
  );
  return -1;
}

int dylinx_forward_condwait(pthread_cond_t *cond, generic_interface_t *lock) {
  generic_interface_t *mtx = (generic_interface_t *)lock;
  return mtx->cond_waiter(cond, mtx->entity);
}
// }}}

DYLINX_EXTERIOR_WRAPPER_IMPLE(pthreadmtx);
#endif // __DYLINX_GLUE__
