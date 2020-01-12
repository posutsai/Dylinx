#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#ifndef WRITER_DURATION
#define WRITER_DURATION 1 << 15
#endif

#ifndef READER_DURATION
#define READER_DURATION 1 << 15
#endif

#define TASK_DURATION 1 << 15

#define N_BARROW 1024
#define N_JOBS 1048576

#define TEN_POW_9 1000000000

#define FOREACH_SYNC_PRIMITIVE(SYNC_PRIMITIVE)	\
		SYNC_PRIMITIVE(none)					\
		SYNC_PRIMITIVE(mutex)					\
        SYNC_PRIMITIVE(rwlock)					\
        SYNC_PRIMITIVE(seqlock)					\
        SYNC_PRIMITIVE(rcu)

#define GENERATE_ENUM(ENUM) ENUM,
#define GENERATE_STRING(STRING) #STRING,

enum sync_primitive {
    FOREACH_SYNC_PRIMITIVE(GENERATE_ENUM)
};

static const char *sync_primitve_str[] = {
	FOREACH_SYNC_PRIMITIVE(GENERATE_STRING)
};

typedef struct task_args {
	int32_t n;
	int32_t cores;
	void *lock;
} task_args_t;

typedef struct op_record {
	float entry;
	float exit;
	int32_t bin;
} op_record_t;

op_record_t g_records[N_JOBS];
int32_t *g_barrows;

pthread_mutex_t *mutex_init() {
	pthread_mutex_t *lock;
	lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	return lock;
}

void mutix_final(pthread_mutex_t *lock) {
	free(lock);
}

void *mutex_task(void *args) {
	struct timespec start_ts, end_ts;
	int32_t i_core = ((task_args_t *)args) -> n;
	pthread_mutex_t *lock = (pthread_mutex_t *)(((task_args_t *)args) -> lock);
	int32_t cores = ((task_args_t *)args) -> cores;
	int32_t iter = N_JOBS / cores;
	iter = i_core < (N_JOBS % cores)? iter + 1: iter;
	for (int n = 0; n < iter; n++) {
		struct timespec start_ts, end_ts;
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start_ts);
		int32_t v, t;
		int32_t bin = rand() % N_BARROW;
		v = g_barrows[bin];
		if (!v) {
			pthread_mutex_lock(lock);
			for (int i = 0; i < TASK_DURATION; i++)
				t++;
			pthread_mutex_unlock(lock);
		}
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end_ts);
		g_records[n * cores + i_core] = (op_record_t) {
			.entry = start_ts.tv_sec + start_ts.tv_nsec * 1. / TEN_POW_9,
			.exit = end_ts.tv_sec + end_ts.tv_nsec * 1. / TEN_POW_9,
			.bin = v
		};
	}
	free(args);
	return NULL;
}

void *none_task(void *args) {
	struct timespec start_ts, end_ts;
	int32_t i_core = ((task_args_t *)args) -> n;
	int32_t cores = ((task_args_t *)args) -> cores;
	int32_t iter = N_JOBS / cores;
	iter = i_core < (N_JOBS % cores)? iter + 1: iter;
	for (int n = 0; n < iter; n++) {
		struct timespec start_ts, end_ts;
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start_ts);
		int32_t v, t;
		int32_t bin = rand() % N_BARROW;
		v = g_barrows[bin];
		for (int i = 0; i < TASK_DURATION; i++)
			t++;
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end_ts);
		g_records[n * cores + i_core] = (op_record_t) {
			.entry = start_ts.tv_sec + start_ts.tv_nsec * 1. / TEN_POW_9,
			.exit = end_ts.tv_sec + end_ts.tv_nsec * 1. / TEN_POW_9,
			.bin = v
		};
	}
	free(args);
	return NULL;
}

pthread_rwlock_t *rwlock_init() {
	pthread_rwlock_t *lock;
	lock = (pthread_rwlock_t *)malloc(sizeof(pthread_rwlock_t));
	return lock;
}

void *rwlock_reader(void *args) {
	struct timespec start_ts, end_ts;
	int32_t i_core = ((task_args_t *)args)->n;
	int32_t cores = ((task_args_t *)args) -> cores;
	pthread_rwlock_t *lock = (pthread_rwlock_t *)(((task_args_t *)args)->lock);
	int32_t iter = N_JOBS / cores;
	iter = i_core < (N_JOBS % cores)? iter + 1: iter;
	for (int n = 0; n < iter; n++) {
		struct timespec start_ts, end_ts;
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start_ts);
		int32_t v, t;
		int32_t bin = rand() % N_BARROW;
		v = g_barrows[bin];
		if (!v) {
			pthread_rwlock_rdlock(lock);
			for (int i = 0; i < TASK_DURATION; i++)
				t++;
			pthread_rwlock_unlock(lock);
		}
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end_ts);
		g_records[n * cores + i_core] = (op_record_t) {
			.entry = start_ts.tv_sec + start_ts.tv_nsec * 1. / TEN_POW_9,
			.exit = end_ts.tv_sec + end_ts.tv_nsec * 1. / TEN_POW_9,
			.bin = v
		};
	}
	free(args);
	return NULL;
}

void *rwlock_writer(void *args) {
	struct timespec start_ts, end_ts;
	int32_t i_core = ((task_args_t *)args) -> n;
	int32_t cores = ((task_args_t *)args) -> cores;
	pthread_rwlock_t *lock = (pthread_rwlock_t *)(((task_args_t *)args)->lock);
	int32_t iter = N_JOBS / cores;
	iter = i_core < (N_JOBS % cores)? iter + 1: iter;
	for (int n = 0; n < iter; n++) {
		struct timespec start_ts, end_ts;
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start_ts);
		int32_t v, t;
		int32_t bin = rand() % N_BARROW;
		v = g_barrows[bin];
		if (!v) {
			pthread_rwlock_wrlock(lock);
			for (int i = 0; i < TASK_DURATION; i++)
				t++;
			pthread_rwlock_unlock(lock);
		}
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end_ts);
		g_records[n * cores + i_core] = (op_record_t) {
			.entry = start_ts.tv_sec + start_ts.tv_nsec * 1. / TEN_POW_9,
			.exit = end_ts.tv_sec + end_ts.tv_nsec * 1. / TEN_POW_9,
			.bin = v
		};
	}
	free(args);
	return NULL;
}

void rwlock_final(pthread_rwlock_t *locks) {
	free(locks);
}

void *(*task_dispatch(int32_t lock_type, float wr_ratio))(void *) {
	switch(lock_type) {
		case none:
			return none_task;
		case mutex:
			return mutex_task; 
		case rwlock:
			if(rand() < wr_ratio)
				return rwlock_writer;
			return rwlock_reader;
		default:
			perror("Currently the lock type is not supported!");
			return NULL;

	}
}
