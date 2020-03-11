#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
// #include "measure.h"
// extern void fun();
// extern uint64_t get_duration(struct timespec *start, struct timespec *end);
extern "C" {
  pthread_mutex_t g_lock;

  void foo() {
    printf("foo function is executed !!!\n");
  }
  void *pthread_task(void *args) {
    printf("pthread_task is executed\n");
    struct timespec test_start, test_end;
    pthread_mutex_lock(&g_lock);
    sleep(2);
    pthread_mutex_unlock(&g_lock);
    return NULL;
  }

  int main(int argc, char *argv[]) {
    pthread_t tid;
    pthread_mutex_init(&g_lock, NULL);
    pthread_create(&tid, NULL, pthread_task, NULL);
    pthread_join(tid, NULL);
    pthread_mutex_destroy(&g_lock);
    return 0;
  }
}
