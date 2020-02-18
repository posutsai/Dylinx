#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <assert.h>
#include <unistd.h>
#include "utils.h"

#define N_JOBS (((uint64_t)1) << 16)
#define N_BARROW 1024 // This factor won't affect the experiment.
#define TASK_DURATION (((uint64_t)1) << 20)
#define TEN_POW_9 1000000000

#define FOREACH_SYNC_PRIMITIVE(SYNC_PRIMITIVE)		\
		SYNC_PRIMITIVE(mutex_TAS)					\
        SYNC_PRIMITIVE(mutex_TTAS)

#define GENERATE_ENUM(ENUM) ENUM,
#define GENERATE_STRING(STRING) #STRING,

enum sync_primitive {
    FOREACH_SYNC_PRIMITIVE(GENERATE_ENUM)
};

static const char *sync_primitve_str[] = {
	FOREACH_SYNC_PRIMITIVE(GENERATE_STRING)
};

typedef struct op_record {
	uint64_t acquiring_ts;
	uint64_t holding_ts;
	uint64_t releasing_ts;
	int32_t bin;
} op_record_t;

typedef struct task_args {
	int32_t n;
	int32_t cores;
	void *lock;
} task_args_t;

int32_t mem_lock(void *addr, size_t size) {
	unsigned long page_size, page_offset;
	page_size = sysconf(_SC_PAGE_SIZE);
	page_offset = (unsigned long) addr % page_size;
	addr -= page_offset;
	size += page_offset;
	return mlock(addr, size);
}

int32_t mem_unlock(void *addr, size_t size) {
	unsigned long page_size, page_offset;
	page_size = sysconf(_SC_PAGE_SIZE);
	page_offset = (unsigned long) addr % page_size;
	addr -= page_offset;
	size += page_offset;
	return munlock(addr, size);
}

op_record_t g_records[N_JOBS];
pthread_barrier_t barr;

void *pthread_task(void *args) {
	static __thread int32_t i_core, cores, iter, i, n, t;
	static __thread struct timespec acquiring_ts, holding_ts, releasing_ts, sleeping_ts;
	/* static __thread struct timespec acquiring_ts, holding_ts, releasing_ts; */
	pthread_mutex_t *lock;
	sleeping_ts.tv_sec = 0;
	sleeping_ts.tv_nsec = 1 << 15; // 2^25
	lock = (pthread_mutex_t *)(((task_args_t *)args) -> lock);
	i_core = ((task_args_t *)args) -> n;
	cores = ((task_args_t *)args) -> cores;
	pthread_barrier_wait(&barr);
	iter = N_JOBS / cores;
	iter = i_core < (N_JOBS % cores)? iter + 1: iter;
	for (n = 0; n < iter; n++) {
		clock_gettime(CLOCK_MONOTONIC, &acquiring_ts);
		pthread_mutex_lock(lock);
		clock_gettime(CLOCK_MONOTONIC, &holding_ts);
		for (i = 0; i < TASK_DURATION; i++)
			t++;
		pthread_mutex_unlock(lock);
		clock_gettime(CLOCK_MONOTONIC, &releasing_ts);
		nanosleep(&sleeping_ts, NULL);
		g_records[n * cores + i_core] = (op_record_t) {
			.acquiring_ts = acquiring_ts.tv_sec * TEN_POW_9 + acquiring_ts.tv_nsec,
			.holding_ts = holding_ts.tv_sec * TEN_POW_9 + holding_ts.tv_nsec,
			.releasing_ts = releasing_ts.tv_sec * TEN_POW_9 + releasing_ts.tv_nsec
		};
	}
	free(args);
}

pthread_mutex_t *mutex_init() {
	pthread_mutex_t *lock;
	lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	return lock;
}

void mutix_final(pthread_mutex_t *lock) {
	free(lock);
}
// To make the experiment as simple as possible, I fix the array in memory
// constantly and try to eliminate the side effect of memory swapping. NUMA
// factor is not considered yet.
//
// TODO:
// 1. Deploy progmatic loading
int main(int argc, char *argv[]) {
	if (argc != 4) {
		perror("The program requires four arguments as probability of entering critical section, number of cores, lock type and reader writer ratio.\n");
		exit(-1);
	}
	float rc_rate = atof(argv[1]);
	int32_t cores = atoi(argv[2]);
	int32_t lock_type = atoi(argv[3]);
	char output_filename[200];
	void *lock = 0;
	switch(lock_type) {
		case mutex_TAS: // Suppose to have maximum overhead
			gen_record_path("mutex_TAS", output_filename, rc_rate, cores);
			lock = (void *)mutex_init();
			break;
		case mutex_TTAS:
			gen_record_path("mutex_TTAS", output_filename, rc_rate, cores);
			lock = (void *)mutex_init();
			break;
		default:
			perror("Input synchronization primitives doesn't match any of available options.");
			exit(-1);
			break;
	}
	pthread_barrier_init(&barr, NULL, (uint32_t)cores);
	printf("%s\n", output_filename);
	memset(g_records, 0, N_JOBS * sizeof(op_record_t));
#ifdef RANDOM_SEED
	srand(RANDOM_SEED);
#else
	srand(time(NULL));
#endif
	if (mem_lock((void *)g_records, N_JOBS * sizeof(op_record_t) == -1))
		perror("Error happens while locking memory in mem_lock function.\n");
	pthread_t tids[cores];
	for (int i = 0; i < cores; i++) {
		task_args_t *args = malloc(sizeof(task_args_t));
		args -> n = i;
		args -> lock = lock;
		args -> cores = cores;
		pthread_create(tids + i, NULL, pthread_task, (void *)args);
	}
	for (int i = 0; i < cores; i++)
		pthread_join(tids[i], NULL);
	FILE *fp = fopen(output_filename, "w+");
	for (int i = 0; i < N_JOBS; i++)
		fprintf(fp, "%ld\t%ld\t%ld\n",
				g_records[i].acquiring_ts,
				g_records[i].holding_ts,
				g_records[i].releasing_ts
		);
	fclose(fp);
	if (mem_unlock((void *)g_records, N_JOBS * sizeof(op_record_t)) == -1)
		perror("Error happens while unlocking memory in mem_unlock function.\n");
	return 0;
}
