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

#define N_BARROW 1024
#define N_JOBS 1 << 25
#define TEN_POW_9 1000000000

#define FOREACH_SYNC_PRIMITIVE(SYNC_PRIMITIVE) \
		SYNC_PRIMITIVE(mutex)   \
        SYNC_PRIMITIVE(rwlock)  \
        SYNC_PRIMITIVE(seqlock)   \
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
	void *locks;
} task_args_t;

typedef struct op_record {
	float entry;
	float exit;
	int32_t bin;
} op_record_t;

op_record_t g_records[N_JOBS];
int32_t *g_barrows;

pthread_mutex_t *mutex_init() {
	pthread_mutex_t *locks;
	locks = (pthread_mutex_t *)malloc(N_BARROW * sizeof(pthread_mutex_t));
	return locks;
}

void mutix_final(pthread_mutex_t *locks) {
	free(locks);
}

void *mutex_task(void *args) {
	struct timespec start_ts, end_ts;
	int32_t n_thread = ((task_args_t *)args) -> n;
	pthread_mutex_t *locks = (pthread_mutex_t *)(((task_args_t *)args) -> locks);
	int32_t t, v;
   	v = rand() % N_BARROW;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start_ts);
	pthread_mutex_lock(locks + g_barrows[v]);
	for (int i = 0; i < READER_DURATION; i++)
		t++;
	pthread_mutex_unlock(locks + g_barrows[v]);
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end_ts);
	g_records[n_thread] = (op_record_t) {
		.entry = start_ts.tv_sec + start_ts.tv_nsec * 1. / TEN_POW_9,
		.exit = end_ts.tv_sec + end_ts.tv_nsec * 1. / TEN_POW_9,
		.bin = v
	};
	free(args);
	return NULL;
}

pthread_rwlock_t *rwlock_init() {
	pthread_rwlock_t *locks;
	locks = (pthread_rwlock_t *)malloc(N_BARROW * sizeof(pthread_rwlock_t));
	return locks;
}

void *rwlock_reader(void *args) {
	struct timespec start_ts, end_ts;
	int32_t n_thread = ((task_args_t *)args)->n;
	pthread_rwlock_t *locks = (pthread_rwlock_t *)(((task_args_t *)args)->locks);
	int32_t t, v;
	v = rand() % N_BARROW;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start_ts);
	pthread_rwlock_rdlock(locks + g_barrows[v]);
	for (int i = 0; i < READER_DURATION; i++)
		t++;
	pthread_rwlock_unlock(locks + g_barrows[v]);
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end_ts);
	g_records[n_thread] = (op_record_t) {
		.entry = start_ts.tv_sec + start_ts.tv_nsec * 1. / TEN_POW_9,
		.exit = end_ts.tv_sec + end_ts.tv_nsec * 1. / TEN_POW_9,
		.bin = v
	};
	free(args);
}

void *rwlock_writer(void *args) {
	struct timespec start_ts, end_ts;
	int32_t n_thread = ((task_args_t *)args)->n;
	pthread_rwlock_t *locks = (pthread_rwlock_t *)(((task_args_t *)args)->locks);
	int32_t t, v;
	v = rand() % N_BARROW;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start_ts);
	pthread_rwlock_wrlock(locks + g_barrows[v]);
	for (int i = 0; i < READER_DURATION; i++)
		t++;
	pthread_rwlock_unlock(locks + g_barrows[v]);
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end_ts);
	g_records[n_thread] = (op_record_t) {
		.entry = start_ts.tv_sec + start_ts.tv_nsec * 1. / TEN_POW_9,
		.exit = end_ts.tv_sec + end_ts.tv_nsec * 1. / TEN_POW_9,
		.bin = v
	};
	free(args);
}

void rwlock_final(pthread_rwlock_t *locks) {
	free(locks);
}

void *(*task_dispatch(int32_t lock_type, float wr_ratio))(void *) {
	switch(lock_type) {
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
