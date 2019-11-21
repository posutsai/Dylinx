#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sched.h>
#include <sys/mman.h>

#define N_BARROW 32
#define N_TASK_EACH_THREAD 16384
#define OPERATION_LEN 1 << 17

typedef struct op_record {
	unsigned long entry;
	unsigned long exit;
	int32_t bin;
} op_record_t;

struct hash_arg {
	int32_t n_thread;
	int32_t num_core;
};

op_record_t *g_records;
size_t g_one_core_size;
int32_t g_barrows[N_BARROW];
cpu_set_t **g_cpuset_p;

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

void *hash(void *args) {
	int32_t n_thread = ((struct hash_arg *)args)->n_thread;
	int32_t num_core = ((struct hash_arg *)args)->num_core;
	op_record_t *rec = g_records + n_thread * N_TASK_EACH_THREAD;
	pid_t pid = getpid();
	for (int n = 0; n < N_TASK_EACH_THREAD; n++) {
		// Reaffine the task to next CPU for simulating context switch
		cpu_set_t *aff = g_cpuset_p[n % num_core];
		/* if (sched_setaffinity(pid, g_one_core_size, aff) == -1) */
		/* 	perror("Affinity error !!"); */

		struct timespec start_ts, end_ts;
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start_ts);
		int32_t v, t;
		int32_t bin = rand() % N_BARROW;
		// Issues may happen here, since the core operations is too short.
		v = g_barrows[bin];
		for (int i = 0; i < OPERATION_LEN; i++)
			t++;
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end_ts);
		rec[n] = (op_record_t) {
			.entry = start_ts.tv_nsec,
			.exit = end_ts.tv_nsec,
			.bin = bin
		};
	}
	free(args);
	return NULL;
}

// To make the experiment as simple as possible, I fix the array in memory
// constantly and try to eliminate the side effect of memory swapping.
int main(int argc, char *argv[]) {
	if (argc != 3) {
		perror("The program requires two arguments as output file name and core number.\n");
		exit(EXIT_FAILURE);
	}
	uint32_t num_core;
	if (!(num_core = atoi(argv[2]))) {
		perror("The number of CPU core should be specified.\n");
		exit(EXIT_FAILURE);
	}
	printf("spawn %d threads.\n", num_core);
	g_one_core_size = CPU_ALLOC_SIZE(1);
	g_cpuset_p = malloc(num_core * sizeof(cpu_set_t *));
	// Supposed to affine to nCm CPUs
	for (int cpu = 0; cpu < num_core; cpu++) {
		g_cpuset_p[cpu] = CPU_ALLOC(1);
		if(g_cpuset_p[cpu] == NULL) {
			perror("Allocation error\n");
			exit(EXIT_FAILURE);
		}
		CPU_ZERO_S(g_one_core_size, g_cpuset_p[cpu]);
		CPU_SET_S(cpu, g_one_core_size, g_cpuset_p[cpu]);
	}

	char output_filename[200];
	sprintf(output_filename, "records/%s.tsv", argv[1]);
	for (int i; i < N_BARROW; i++)
		g_barrows[i] = rand();

#ifdef RANDOM_SEED
	srand(RANDOM_SEED);
#else
	srand(time(NULL));
#endif

	g_records = malloc(N_TASK_EACH_THREAD * num_core * sizeof(op_record_t));
	memset(g_records, 0, N_TASK_EACH_THREAD * num_core * sizeof(op_record_t));
	if (mem_lock((void *)g_barrows, N_BARROW * sizeof(int32_t)) == -1 ||
		mem_lock((void *)g_records, N_TASK_EACH_THREAD * num_core * sizeof(op_record_t) == -1))
		perror("Error happens while locking memory in mem_lock function.\n");
	pthread_t tids[num_core];
	for (int i = 0; i < num_core; i++) {
		struct hash_arg *arg = malloc(sizeof(struct hash_arg));
		arg->n_thread = i;
		arg->num_core = num_core;
		pthread_create(tids + i, NULL, hash, (void *)arg);
	}

	for (int i = 0; i < num_core; i++) {
		pthread_join(tids[i], NULL);
		CPU_FREE(g_cpuset_p[i]);
	}
	FILE *fp = fopen(output_filename, "w+");
	for (int i = 0; i < N_TASK_EACH_THREAD * num_core; i++)
		fprintf(fp, "%lu\t%lu\t%d\n", g_records[i].entry, g_records[i].exit, g_records[i].bin);
		/* printf("%lu\t%lu\t%d\n", g_records[i].entry, g_records[i].exit, g_records[i].bin); */
	fclose(fp);
	if (mem_unlock((void *)g_barrows, N_BARROW * sizeof(int)) == -1 ||
		mem_unlock((void *)g_records, N_TASK_EACH_THREAD * num_core * sizeof(op_record_t)) == -1)
		perror("Error happens while unlocking memory in mem_unlock function.\n");
	free(g_cpuset_p);
	free(g_records);
	return 0;
}
