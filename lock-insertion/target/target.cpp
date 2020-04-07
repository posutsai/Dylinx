#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include "bar.h"

extern "C" {
  pthread_mutex_t g_lock1, g_lock2; //! [LockSlot] TTAS

  void *foo(void *args) {
    sleep(3);
    pthread_mutex_lock(&g_lock1);
    pthread_mutex_lock(&g_lock1);
    sleep(2);
    pthread_mutex_unlock(&g_lock1);
    bar();
    return 0;
  }

  void haa(pthread_mutex_t mutex) {}

  int main(int argc, char *argv[]) {
    pthread_mutex_t main_lock; //! [LockSlot] TTAS, MUTEX, SPINLOCK
    pthread_t tid;
    pthread_mutex_init(&g_lock1, NULL);
    pthread_create(&tid, NULL, foo, NULL);
    pthread_join(tid, NULL);
    pthread_mutex_destroy(&g_lock1);
    return 0;
  }
}
