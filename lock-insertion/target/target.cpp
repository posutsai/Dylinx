#include <stdio.h>
#include <pthread.h>
// #include <time.h>
#include <unistd.h>
// hahahhaah
extern "C" {
  pthread_mutex_t g_lock;

  void *foo(void *args) {
    sleep(3);
    /**
     *  this is comment
     */
    /*! \file hah
     *
     */
    /// comment
    /// \foo
    /// hello
    /// \foo
    pthread_mutex_lock(&g_lock); //! ttas
    sleep(2);
    pthread_mutex_unlock(&g_lock);
    return NULL;
  }

  int main(int argc, char *argv[]) {
    pthread_t tid;
    pthread_mutex_init(&g_lock, NULL);
    pthread_create(&tid, NULL, foo, NULL);
    pthread_join(tid, NULL);
    pthread_mutex_destroy(&g_lock);
    return 0;
  }
}
