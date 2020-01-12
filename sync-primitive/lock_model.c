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
#include "reader_writer.h"
#include "utils.h"

#define N_THREAD 16384
#define N_BARROW 1024 // This factor won't affect the experiment.

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

int32_t *g_barrows;

// To make the experiment as simple as possible, I fix the array in memory
// constantly and try to eliminate the side effect of memory swapping. NUMA
// factor is not considered yet.
//
// TODO:
// 1. Deploy progmatic loading
int main(int argc, char *argv[]) {
	if (argc != 5) {
		perror("The program requires four arguments as probability of entering critical section, number of cores, lock type and reader writer ratio.\n");
		exit(-1);
	}
	float rc_rate = atof(argv[1]);
	int32_t cores = atoi(argv[2]);
	int32_t lock_type = atoi(argv[3]);
	float wr_ratio = atof(argv[4]);
	char output_filename[200];
	g_barrows = (int32_t *)malloc(N_BARROW * sizeof(int32_t));
	void *lock = 0;
	switch(lock_type) {
		case none:
			gen_record_path("none", output_filename, rc_rate, cores);
		case mutex:
			gen_record_path("mutex", output_filename, rc_rate, cores);
			lock = (void *)mutex_init();
			break;
		case rwlock:
			gen_record_path("rwlock", output_filename, rc_rate, cores);
			lock = (void *)rwlock_init();
			break;
		case seqlock:
			gen_record_path("seqlock", output_filename, rc_rate, cores);
			break;
		case rcu:
			gen_record_path("rcu", output_filename, rc_rate, cores);
			break;
		default:
			perror("Input synchronization primitives doesn't match any of available options.");
			exit(-1);
			break;
	}
	printf("%s\n", output_filename);
	for (int i = 0; i < N_BARROW; i++)
		if (i < N_BARROW * rc_rate)
			g_barrows[i] = 0;
		else
			g_barrows[i] = i;
	memset(g_records, 0, N_JOBS * sizeof(op_record_t));
#ifdef RANDOM_SEED
	srand(RANDOM_SEED);
#else
	srand(time(NULL));
#endif
	if (mem_lock((void *)g_barrows, N_BARROW * sizeof(int32_t)) == -1 ||
		mem_lock((void *)g_records, N_THREAD * sizeof(op_record_t) == -1))
		perror("Error happens while locking memory in mem_lock function.\n");
	pthread_t tids[cores];
	for (int i = 0; i < cores; i++) {
		task_args_t *args = malloc(sizeof(task_args_t));
		args -> n = i;
		args -> lock = lock;
		args -> cores = cores;
		pthread_create(tids + i, NULL, task_dispatch(lock_type, wr_ratio), (void *)args);
	}
	for (int i = 0; i < cores; i++)
		pthread_join(tids[i], NULL);
	return 0;
	FILE *fp = fopen(output_filename, "w+");
	for (int i = 0; i < N_THREAD; i++)
		fprintf(fp, "%f\t%f\t%d\n", g_records[i].entry, g_records[i].exit, g_records[i].bin);
	fclose(fp);
	if (mem_unlock((void *)g_barrows, N_BARROW * sizeof(int)) == -1 ||
		mem_unlock((void *)g_records, N_JOBS * sizeof(op_record_t)) == -1)
		perror("Error happens while unlocking memory in mem_unlock function.\n");
	free(g_barrows);
	return 0;
}
