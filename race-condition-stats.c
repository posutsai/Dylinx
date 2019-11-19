#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>

#define N_BARROW 256
#define N_THREAD 16384

typedef struct op_record {
	unsigned long entry;
	unsigned long exit;
	int32_t bin;
} op_record_t;

op_record_t g_records[N_THREAD];
int32_t g_barrows[N_BARROW];

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
	for (int i = 0; i < 16384; i++)
		t++;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end_ts);
	g_records[*((int32_t *)n_thread)] = (op_record_t) {
		.entry = start_ts.tv_nsec,
		.exit = end_ts.tv_nsec,
		.bin = bin
	};
	free(n_thread);
	return NULL;
}

// To make the experiment as simple as possible, I fix the array in memory
// constantly and try to eliminate the side effect of memory swapping.
int main(int argc, char *argv[]) {
	if (argc != 2) {
		perror("The program requires a argument as output file name.\n");
		exit(-1);
	}
	char output_filename[200];
	sprintf(output_filename, "records/%s.tsv", argv[1]);
	fprintf(stderr, "output records to %s\n", output_filename);
	for (int i; i < N_BARROW; i++)
		g_barrows[i] = rand();
	fprintf(stderr, "start mem setting\n");
	memset(g_records, 0, N_THREAD * sizeof(op_record_t));
#ifdef RANDOM_SEED
	srand(RANDOM_SEED);
#else
	srand(time(NULL));
#endif
	fprintf(stderr, "start mem locking\n");
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
		fprintf(fp, "%lu\t%lu\t%d\n", g_records[i].entry, g_records[i].exit, g_records[i].bin);
		/* printf("%lu\t%lu\t%d\n", g_records[i].entry, g_records[i].exit, g_records[i].bin); */
	fclose(fp);
	if (mem_unlock((void *)g_barrows, N_BARROW * sizeof(int)) == -1 ||
		mem_unlock((void *)g_records, N_THREAD * sizeof(op_record_t)) == -1)
		perror("Error happens while unlocking memory in mem_unlock function.\n");
	return 0;
}
