#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include "func.h"

typedef struct MtxType {
    //! [LockSlot] TTAS
    pthread_mutex_t a;
    //! [LockSlot] PTHREADMTX
    pthread_mutex_t *b;
}my_type_t;

typedef pthread_mutex_t MyLock;
void foo_int(int a) {}
void foo_double(float a) {}
void fun(pthread_mutex_t *mtx) { }

pthread_mutex_t *freeze_state(pthread_mutex_t *mtx) { return NULL; }

int main(int argc, char *argv[]) {
  pthread_mutex_t *mtx1 = malloc(sizeof(pthread_mutex_t));
  pthread_mutex_t *mtx3;
  mtx3 =  malloc(sizeof(pthread_mutex_t));
  pthread_mutex_t mtx2;
  fun(mtx1);
  fun(&mtx2);
  int a = 0;
  mtx_cycle();
  return 0;
}
