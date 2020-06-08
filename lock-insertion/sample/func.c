#include "glue.h"
#include "func.h"

void mtx_cycle() {
    pthread_mutex_t mtx1;
    pthread_mutex_t mtx2;
    pthread_mutex_t mtx3;
    pthread_mutex_t mtx4;
    pthread_mutex_t mtx5;
    pthread_mutex_init(&mtx1, NULL);
    pthread_mutex_lock(&mtx1);
    pthread_mutex_unlock(&mtx1);
    pthread_mutex_destroy(&mtx1);
}
