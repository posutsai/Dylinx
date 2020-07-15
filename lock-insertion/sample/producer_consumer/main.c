#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>

#define ITER_CNT 5

int32_t total_produce = 0;
int32_t total_consume = 0;
int32_t available = 0;
pthread_mutex_t mtx;
pthread_cond_t cond;

void *produce(void *args) {
    while(total_produce < ITER_CNT) {
        pthread_mutex_lock(&mtx);
        total_produce++;
        available++;
        printf("Generate a product!!\n");
        pthread_mutex_unlock(&mtx);
        pthread_cond_signal(&cond);
        sleep(2);
    }
    return NULL;
}

void *consume(void *args) {
    while(total_consume < ITER_CNT) {
        pthread_mutex_lock(&mtx);
        while(!available) {
            // Wait here until there is available product.
            printf("Consumer is awaked but no data left.\n");
            pthread_cond_wait(&cond, &mtx);
        }
        printf("Consume a product\n");
        available--;
        total_consume++;
        pthread_mutex_unlock(&mtx);
        sleep(1);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    pthread_t producer, consumer;
    pthread_mutex_init(&mtx, NULL);
    pthread_cond_init(&cond, NULL);
    pthread_create(&producer, NULL, produce, NULL);
    pthread_create(&consumer, NULL, consume, NULL);
    pthread_join(consumer, NULL);
    pthread_join(producer, NULL);
    pthread_mutex_destroy(&mtx);
    pthread_cond_destroy(&cond);
	return 0;
}
