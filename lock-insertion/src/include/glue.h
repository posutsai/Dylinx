#include <cstdint>
#include <pthread.h>
#include <cstdio>
#include <typeinfo>
class BaseLock {
public:
  BaseLock() {}
  BaseLock(pthread_mutex_t *l) {
    printf("pthread_mutex_lock is converted to baselock!!!\n");
  }
  int init() {
    fprintf(stderr, "Do not use BaseLock!!!\n");
    return -1;
  }
  int lock() {
    fprintf(stderr, "Do not use BaseLock!!!\n");
    return -1;
  }
  int unlock() {
    fprintf(stderr, "Do not use BaseLock!!!\n");
    return -1;
  }
  int destroy() {
    fprintf(stderr, "Do not use BaseLock!!!\n");
    return -1;
  }
};

class TTASLock: public BaseLock {
public:
  int init() {
    printf("TTASLock instance is initialized!!!!\n");
    return 0;
  }
};
//
// class MutexLock: public BaseLock {
// public:
//   int init() {
//     printf("MutexLock instance is initialized!!!\n");
//     return 0;
//   }
// };

int ___dylinx_slot_init_(pthread_mutex_t *lock, const pthread_mutexattr_t *attr) { return pthread_mutex_init(lock, NULL); }
int ___dylinx_slot_lock_(pthread_mutex_t *lock) { return pthread_mutex_lock(lock); }
int ___dylinx_slot_unlock_(pthread_mutex_t *lock) { return pthread_mutex_unlock(lock); }
int ___dylinx_slot_destroy_(pthread_mutex_t *lock) { return pthread_mutex_destroy(lock);}

int ___dylinx_slot_init_(BaseLock *lock, const pthread_mutexattr_t *attr) { return lock->init(); }
int ___dylinx_slot_lock_(BaseLock *lock) { return lock->lock(); }
int ___dylinx_slot_unlock_(BaseLock *lock) { return lock->unlock(); }
int ___dylinx_slot_destroy_(BaseLock *lock) { return lock->destroy(); }
// int main() {
  // pthread_mutex_t m, l;
  // pthread_mutex_t p;
  // MutexLock m;
  // TTASLock l;
  // slot_init(&l);
  // slot_init(&m);
  // slot_init(&p);
  // pthread_mutex_init(&l);
  // pthread_mutex_lock(&l);
  // return 0;
// }
