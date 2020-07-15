#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
//! [LockSlot] TTAS, BACKOFF
pthread_mutex_t mtx1 = PTHREAD_MUTEX_INITIALIZER;
//! [LockSlot] TTAS, BACKOFF
pthread_mutex_t mtx2 = PTHREAD_MUTEX_INITIALIZER;

int counter1 = 0;
int counter2 = 0;

void *task(void *args) {
  for (int i = 0; i < 1000; i++) {
    pthread_mutex_lock(&mtx1);
    counter1++;
    pthread_mutex_unlock(&mtx1);
    usleep(rand() % 1000);
  }
  for (int i = 0; i < 1000; i++) {
    pthread_mutex_lock(&mtx2);
    counter2++;
    pthread_mutex_unlock(&mtx2);
    usleep(rand() % 1000);
  }
  return 0;
}

int main(int argc, char *argv[]) {
  pthread_t threads[64];
  srand(time(NULL));
  for (int i = 0; i < 64; i++)
    pthread_create(&threads[i], NULL, task, NULL);
  for (int i = 0; i < 64; i++)
    pthread_join(threads[i], NULL);
  printf("Two counters are %d, %d\n", counter1, counter2);
  return 0;
}
