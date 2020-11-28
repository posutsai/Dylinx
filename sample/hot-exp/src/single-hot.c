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
#include <sys/time.h>
#include "loading.h"

#define REPEAT 1 << 9
pthread_mutex_t mtx ;
 __attribute__((xray_never_instrument)) void *parallel_op(void *args) {
    for (int i = 0; i < REPEAT; i++) {
        parallel_section();
        critical_section();
        parallel_section();
    }
    return NULL;
}

double te2double(struct timeval te) {
    return te.tv_sec * 1. + te.tv_usec / 1000000.;
}

int main(int argc, char *argv[]) {
    pid_t tid = syscall(SYS_gettid);
    uint32_t n_core = get_nprocs();
    struct timeval start_te, end_te;
    pthread_t threads[n_core];
    gettimeofday(&start_te, NULL);
    for (uint32_t i = 0; i < n_core; i++)
        pthread_create(&threads[i], NULL, parallel_op, NULL);
    for (uint32_t i = 0; i < n_core; i++)
        pthread_join(threads[i], NULL);
    gettimeofday(&end_te, NULL);
    double duration = te2double(end_te) - te2double(start_te);
    printf("duration %fs\n", duration);
    pthread_mutex_destroy(&mtx);
	return 0;
}
