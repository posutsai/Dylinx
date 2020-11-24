#include <stdio.h>
#include <x86intrin.h>
#include <float.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include "loading.h"

#define TEST_TIME 10

typedef struct {
    float mean;
    float std;
    float max;
    float min;
} stable_index_t;

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
    printf("Duration ratio, cs: %f pa: %f\n", CS_RATIO, 1 - CS_RATIO);
    pid_t tid = syscall(SYS_gettid);
    float ratio[TEST_TIME];
    stable_index_t index;
    for (int i = 0; i < TEST_TIME; i++) {
        uint64_t start = __rdtsc();
        parallel_section();
        uint64_t cs_start = __rdtsc();
        float limit = TOTAL_NUM_OP * CS_RATIO;
        for (uint32_t i = 0; i < limit; i++) {
            __asm__ volatile("" : "+g" (i) : :);
        }
        uint64_t cs_end = __rdtsc();
        parallel_section();
        uint64_t end = __rdtsc();
        uint64_t total_dur = end - start;
        uint64_t cs_dur = cs_end - cs_start;
        ratio[i] = cs_dur * 1. / total_dur;
    }
    get_mean_std(ratio, TEST_TIME, &index);
    printf("mean: %f, std: %f, max: %f, min: %f, main_tid: %d\n", index.mean, index.std, index.max, index.min, tid);
	return 0;
}
