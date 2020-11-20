#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <math.h>
#include <errno.h>
#include <x86intrin.h>
#include <float.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>

/* #define TOTAL_NUM_OP (1 << 30) * 1. */
#define TOTAL_NUM_OP (1 << 13) * 1.
#ifndef CS_RATIO
#   error Error happens because CS_RATIO macro is not defined
#endif

#define REPEAT 1 << 14
#define TEST_TIME 10

typedef struct {
    float mean;
    float std;
    float max;
    float min;
} stable_index_t;

pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

__attribute__((xray_always_instrument)) void critical_load() {
    float limit = TOTAL_NUM_OP * CS_RATIO;
    for (uint32_t i = 0; i < limit; i++) {
        __asm__ volatile("" : "+g" (i) : :);
    }
}

__attribute__((xray_always_instrument)) void critical_section() {
    critical_load();
#ifdef WITH_MUTEX
    pthread_mutex_unlock(&mtx);
#endif
}

__attribute__((xray_never_instrument)) void parallel_section_head() {
    float limit = TOTAL_NUM_OP * ((1 - CS_RATIO) / 2.);
    for (uint32_t i = 0; i < limit; i++) {
        __asm__ volatile("" : "+g" (i) : :);
    }
#ifdef WITH_MUTEX
    pthread_mutex_lock(&mtx);
#endif
}

__attribute__((xray_never_instrument)) void parallel_section_bottom() {
    float limit = TOTAL_NUM_OP * ((1 - CS_RATIO) / 2.);
    for (uint32_t i = 0; i < limit; i++) {
        __asm__ volatile("" : "+g" (i) : :);
    }
}

 __attribute__((xray_never_instrument)) void *parallel_op(void *args) {
    pid_t tid = syscall(SYS_gettid);
    printf("spawn_tid: %d\n", tid);
    for (int i = 0; i < REPEAT; i++) {
        parallel_section_head();
        critical_section();
        parallel_section_bottom();
    }
    return NULL;
}

void get_mean_std(float *ratio, int32_t len, stable_index_t *index) {
    index->min = FLT_MAX;
    index->max = FLT_MIN;
    float total = 0.;
    for (int i = 0; i < len; i++) {
        if (ratio[i] > index->max)
            index->max = ratio[i];
        if (ratio[i] < index->min)
            index->min = ratio[i];
        total += ratio[i];
    }
    float mean = total / len;
    float sd = 0.;
    for (int i = 0; i < len; i++)
        sd += pow(ratio[i] - mean, 2);
    index->mean = mean;
    index->std = sqrt(sd / len);
}

int main(int argc, char *argv[]) {
    pid_t tid = syscall(SYS_gettid);
#ifdef TEST_DURATION
    printf("Duration ratio, cs: %f pa: %f\n", CS_RATIO, 1 - CS_RATIO);
    float ratio[TEST_TIME];
    stable_index_t index;
    for (int i = 0; i < TEST_TIME; i++) {
        uint64_t start = __rdtsc();
        parallel_section_head();
        uint64_t cs_start = __rdtsc();
        critical_section();
        uint64_t cs_end = __rdtsc();
        parallel_section_bottom();
        uint64_t end = __rdtsc();
        uint64_t total_dur = end - start;
        uint64_t cs_dur = cs_end - cs_start;
        ratio[i] = cs_dur * 1. / total_dur;
    }
    get_mean_std(ratio, TEST_TIME, &index);
    printf("mean: %f, std: %f, max: %f, min: %f, main_tid: %d\n", index.mean, index.std, index.max, index.min, tid);
    if (index.std > 0.002)
        printf("ERROR std value is so high that the exp may be unstable.\n");
#endif // TEST_DURATION
    printf("main_tid: %d\n", tid);
    uint32_t n_core = get_nprocs();
    pthread_t threads[n_core];
    for (uint32_t i = 0; i < n_core; i++)
        pthread_create(&threads[i], NULL, parallel_op, NULL);
    for (uint32_t i = 0; i < n_core; i++)
        pthread_join(threads[i], NULL);
    pthread_mutex_destroy(&mtx);
	return 0;
}