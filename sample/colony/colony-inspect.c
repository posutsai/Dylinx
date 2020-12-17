#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/sysinfo.h>

#define REPEAT_A 1 << 3
#define REPEAT_B 1 << 3
#define REPEAT_C 1 << 3
pthread_mutex_t mtx_a;
pthread_mutex_t mtx_b0;
pthread_mutex_t mtx_b1;
pthread_mutex_t mtx_c0;
pthread_mutex_t mtx_c1;
pthread_mutex_t mtx_c2;

void *parallel_op_a(void *arg) {
    for (int i = 0; i < REPEAT_A; i++) {
        // Parallel section A
        for (uint32_t j = 0; j < (1 << 10); j++)
            __asm__ volatile("" : "+g" (j) : :);
        // Critical section A
        pthread_mutex_lock(&mtx_a);
        for (uint32_t j = 0; j < (1 <<  7); j++)
            __asm__ volatile("" : "+g" (j) : :);
        pthread_mutex_unlock(&mtx_a);
    }
    return NULL;
}

void *parallel_op_b(void *arg) {
    for (int i = 0; i < REPEAT_B; i++) {
        // Parallel section B
        for (uint32_t j = 0; j < (1 << 10); j++)
            __asm__ volatile("" : "+g" (j) : :);
        if(rand() % 2) {
            // Critical section B0
            pthread_mutex_lock(&mtx_b0);
            for (uint32_t j = 0; j < (1 <<  8); j++)
                __asm__ volatile("" : "+g" (j) : :);
            pthread_mutex_unlock(&mtx_b0);
        } else {
            // Critical section B1
            pthread_mutex_lock(&mtx_b1);
            for (uint32_t j = 0; j < (1 <<  9); j++)
                __asm__ volatile("" : "+g" (j) : :);
            pthread_mutex_unlock(&mtx_b1);
        }
    }
    return NULL;
}

void *parallel_op_c(void *arg) {
    for (int i = 0; i < REPEAT_C; i++) {
        // Parallel section B
        for (uint32_t j = 0; j < (1 << 10); j++)
            __asm__ volatile("" : "+g" (j) : :);
        int r = rand() % 3;
        if(r == 2) {
            // Critical section B0
            pthread_mutex_lock(&mtx_c0);
            for (uint32_t j = 0; j < (1 << 10); j++)
                __asm__ volatile("" : "+g" (j) : :);
            pthread_mutex_unlock(&mtx_c0);
        } else if(r == 1) {
            // Critical section B0
            pthread_mutex_lock(&mtx_c1);
            for (uint32_t j = 0; j < (1 << 10); j++)
                __asm__ volatile("" : "+g" (j) : :);
            pthread_mutex_unlock(&mtx_c1);
        } else {
            // Critical section B1
            pthread_mutex_lock(&mtx_c2);
            for (uint32_t j = 0; j < (1 << 10); j++)
                __asm__ volatile("" : "+g" (j) : :);
            pthread_mutex_unlock(&mtx_c2);
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    srand(time(0));
    pthread_mutex_init(&mtx_a, NULL);
    pthread_mutex_init(&mtx_b0, NULL);
    pthread_mutex_init(&mtx_b1, NULL);
    pthread_mutex_init(&mtx_c0, NULL);
    pthread_mutex_init(&mtx_c1, NULL);
    pthread_mutex_init(&mtx_c2, NULL);
    uint32_t n_core = get_nprocs_conf();
    pthread_t threads[n_core];

    for (uint32_t i = 0; i < n_core; i++) {
        pthread_create(&threads[i], NULL, parallel_op_a, NULL);
    }
    for (uint32_t i = 0; i < n_core; i++)
        pthread_join(threads[i], NULL);

    for (uint32_t i = 0; i < n_core; i++) {
        pthread_create(&threads[i], NULL, parallel_op_b, NULL);
    }
    for (uint32_t i = 0; i < n_core; i++)
        pthread_join(threads[i], NULL);

    for (uint32_t i = 0; i < n_core; i++) {
        pthread_create(&threads[i], NULL, parallel_op_c, NULL);
    }
    for (uint32_t i = 0; i < n_core; i++)
        pthread_join(threads[i], NULL);

    pthread_mutex_destroy(&mtx_a);
    pthread_mutex_destroy(&mtx_b0);
    pthread_mutex_destroy(&mtx_b1);
    pthread_mutex_destroy(&mtx_c0);
    pthread_mutex_destroy(&mtx_c1);
    pthread_mutex_destroy(&mtx_c2);
	return 0;
}
