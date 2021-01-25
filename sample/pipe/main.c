#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <x86intrin.h>
#include <unistd.h>
#include <sys/time.h>
#include "pipe.h"
#define BULK_SIZE 1024
#define REPEAT 128
#define TOTAL_PUSH_OP 64 * REPEAT * BULK_SIZE / N_THREAD
#define TORAL_POP_OP 64 * REPEAT / N_THREAD

void *producer_op(void *arg) {
    pipe_producer_t *handler = (pipe_producer_t *)arg;
    int buff = 1;
    for (int t = 0; t < TOTAL_PUSH_OP; t++) {
        pipe_push(handler, &buff, sizeof(int));
    }
    return NULL;
}

void *consumer_op(void *arg) {
    pipe_consumer_t *handler = (pipe_consumer_t *)arg;
    int buff[BULK_SIZE];
    for (int t = 0; t < TORAL_POP_OP; t++)
        pipe_pop(handler, buff, BULK_SIZE * sizeof(int));
    return NULL;
}

int main(int argc, char *argv[]) {
    pthread_t producers[N_THREAD];
    pthread_t consumers[N_THREAD];
    pipe_t *q = pipe_new(1, 0);
    pipe_producer_t *pushers[N_THREAD];
    pipe_consumer_t *poppers[N_THREAD];
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    for (int i = 0; i < N_THREAD; i++) {
        pushers[i] = pipe_producer_new(q);
    }
    for (int i = 0; i < N_THREAD; i++) {
        poppers[i] = pipe_consumer_new(q);
    }
    for (int i = 0; i < N_THREAD; i++)
        pthread_create(&producers[i], NULL, producer_op, pushers[i]);
    for (int i = 0; i < N_THREAD; i++)
        pthread_create(&consumers[i], NULL, consumer_op, poppers[i]);
    for (int i = 0; i < N_THREAD; i++) {
        pthread_join(producers[i], NULL);
        pthread_join(consumers[i], NULL);
    }
    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    uint64_t delta_us = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_nsec - start.tv_nsec) / 1000;
    printf("Duration is %lu\n", delta_us);
	return 0;
}
