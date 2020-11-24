#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include "loading.h"

#define REPEAT 1 << 8
pthread_mutex_t mtx ;
 __attribute__((xray_never_instrument)) void *parallel_op(void *args) {
    for (int i = 0; i < REPEAT; i++) {
        parallel_section();
        critical_section();
        parallel_section();
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    pid_t tid = syscall(SYS_gettid);
    uint32_t n_core = get_nprocs();
    pthread_t threads[n_core];
    for (uint32_t i = 0; i < n_core; i++)
        pthread_create(&threads[i], NULL, parallel_op, NULL);
    for (uint32_t i = 0; i < n_core; i++)
        pthread_join(threads[i], NULL);
    pthread_mutex_destroy(&mtx);
	return 0;
}
