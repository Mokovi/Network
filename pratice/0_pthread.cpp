#include <iostream>
#include <pthread.h>

void* func(void *arg) {
    printf("Hello world.\n");
    static int k = *(int *)arg;
    printf("Arg = %d\n", k);
    return (void *)&k;
}

int main() {
    pthread_t thread;
    int k = 10;
    if (pthread_create(&thread, NULL, func, (void *)&k) != 0) {
        perror("fail to pthread_create");
        exit(1);
    }
    int* retrunVal;
    pthread_join(thread, (void**)&retrunVal);
    printf("Return Val = %d\n", *retrunVal);
    std::cout <<"End.\n";
    return 0;
}