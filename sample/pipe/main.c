#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <x86intrin.h>
#include <unistd.h>
#include "pipe.h"
#define N_THREAD 64
#define BULK_SIZE 1024
#define REPEAT 128

void *producer_op(void *arg) {
    pipe_producer_t *handler = (pipe_producer_t *)arg;
    int buff = 1;
    for (int t = 0; t < BULK_SIZE * REPEAT; t++) {
        pipe_push(handler, &buff, sizeof(int));
    }
    return NULL;
}

void *consumer_op(void *arg) {
    pipe_consumer_t *handler = (pipe_consumer_t *)arg;
    int buff[BULK_SIZE];
    for (int t = 0; t < REPEAT; t++)
        pipe_pop(handler, buff, BULK_SIZE * sizeof(int));
    return NULL;
}

int main(int argc, char *argv[]) {
    pthread_t producers[N_THREAD];
    pthread_t consumers[N_THREAD];
    pipe_t *q = pipe_new(1, 0);
    pipe_producer_t *pushers[N_THREAD];
    pipe_consumer_t *poppers[N_THREAD];
    uint64_t timer_s = __rdtsc();
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
    uint64_t timer_e = __rdtsc();
    printf("Duration is %lu\n", timer_e - timer_s);
	return 0;
}
