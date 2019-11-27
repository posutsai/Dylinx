#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>

#define N_THREAD 16384
#define N_BARROW 128
#define TEN_POW_9 1000000000

typedef struct op_record {
	float entry;
	float exit;
	int32_t bin;
} op_record_t;

op_record_t g_records[N_THREAD];
int32_t *g_barrows;
int32_t g_op_length = 0;

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

void *hash(void *n_thread) {
	struct timespec start_ts, end_ts;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start_ts);
	int32_t v, t;
	int32_t bin = rand() % N_BARROW;
	// Issues may happen here, since the core operations is too short.
	v = g_barrows[bin];
	for (int i = 0; i < g_op_length; i++)
		t++;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end_ts);
	g_records[*((int32_t *)n_thread)] = (op_record_t) {
		.entry = start_ts.tv_sec + start_ts.tv_nsec * 1. / TEN_POW_9,
		.exit = end_ts.tv_sec + end_ts.tv_nsec * 1. / TEN_POW_9,
		.bin = v
	};
	free(n_thread);
	return NULL;
}

// To make the experiment as simple as possible, I fix the array in memory
// constantly and try to eliminate the side effect of memory swapping. NUMA
// factor is not considered yet.
int main(int argc, char *argv[]) {
	if (argc != 4) {
		perror("The program requires two arguments as number of barrows, operation length and number of cores.\n");
		exit(-1);
	}
	float rc_rate = atof(argv[1]);
	g_op_length = atoi(argv[2]);
	int32_t cores = atoi(argv[3]);
	char output_filename[200];
	sprintf(output_filename, "records/%.3f_%d_%d_v%d.tsv", rc_rate, g_op_length, cores, 0);
	for (uint32_t v = 0; access( output_filename, F_OK ) != -1; v++)
		sprintf(output_filename, "records/%.3f_%d_%d_v%d.tsv", rc_rate, g_op_length, cores, v);
	printf("%s\n", output_filename);
	g_barrows = (int32_t *)malloc(N_BARROW * sizeof(int32_t));
	for (int i = 0; i < N_BARROW; i++)
		if (i < N_BARROW * rc_rate)
			g_barrows[i] = 0;
		else
			g_barrows[i] = i;
	memset(g_records, 0, N_THREAD * sizeof(op_record_t));
#ifdef RANDOM_SEED
	srand(RANDOM_SEED);
#else
	srand(time(NULL));
#endif
	if (mem_lock((void *)g_barrows, N_BARROW * sizeof(int32_t)) == -1 ||
		mem_lock((void *)g_records, N_THREAD * sizeof(op_record_t) == -1))
		perror("Error happens while locking memory in mem_lock function.\n");
	pthread_t tids[N_THREAD];
	for (int i = 0; i < N_THREAD; i++) {
		int32_t *n = malloc(sizeof(int));
		*n = i;
		pthread_create(tids + i, NULL, hash, (void *)n);
	}
	for (int i = 0; i < N_THREAD; i++)
		pthread_join(tids[i], NULL);
	FILE *fp = fopen(output_filename, "w+");
	for (int i = 0; i < N_THREAD; i++)
		fprintf(fp, "%f\t%f\t%d\n", g_records[i].entry, g_records[i].exit, g_records[i].bin);
	fclose(fp);
	if (mem_unlock((void *)g_barrows, N_BARROW * sizeof(int)) == -1 ||
		mem_unlock((void *)g_records, N_THREAD * sizeof(op_record_t)) == -1)
		perror("Error happens while unlocking memory in mem_unlock function.\n");
	free(g_barrows);
	return 0;
}
