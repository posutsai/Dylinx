#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

// extern "C" {
  void *pthread_task(void *args) {
    sleep(4);
    return NULL;
  }

  int main(int argc, char *argv[]) {
    printf("haha\n");
    pthread_t tid;
    pthread_create(&tid, NULL, pthread_task, NULL);
    pthread_join(tid, NULL);
    return 0;
  }
// }
