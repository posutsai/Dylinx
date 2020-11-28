#include <pthread.h>
#include <math.h>
#include <stdlib.h>

extern pthread_mutex_t mtx;

#ifndef CS_DURATION_EXP
#   error Error happens because CS_DURATION_EXP macro is not defined
#endif

#define TOTAL_NUM_OP (1 << CS_DURATION_EXP) * 1.
#ifndef CS_RATIO
#   error Error happens because CS_RATIO macro is not defined
#endif

__attribute__((xray_always_instrument)) void critical_load() {
    float limit = TOTAL_NUM_OP;
    for (uint32_t i = 0; i < limit; i++) {
        __asm__ volatile("" : "+g" (i) : :);
    }
}

__attribute__((xray_always_instrument)) void critical_section() {
#ifdef WITH_MUTEX
    pthread_mutex_lock(&mtx);
#endif
    critical_load();
#ifdef WITH_MUTEX
    pthread_mutex_unlock(&mtx);
#endif
}

__attribute__((xray_never_instrument)) void parallel_section() {
    float limit = TOTAL_NUM_OP * ((1 - CS_RATIO) / 2.) + rand() % 1000;
    for (uint32_t i = 0; i < limit; i++) {
        __asm__ volatile("" : "+g" (i) : :);
    }
}
