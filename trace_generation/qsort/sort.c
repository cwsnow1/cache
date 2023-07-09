#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#define SIZE (100000)

void usage(void) {
    printf("Give the number of ints to sort, please!\n");
    exit(1);
}

int cmpfunc (const void * a, const void * b) {
   return ( *(int*)a - *(int*)b );
}

void randomize(size_t *array, long int size) {
    for (long i = 0; i < size; i++) {
        array[i] = rand() % size;
    }
}

int main(int argc, char** argv) {

    static size_t array[SIZE];
    randomize(array, SIZE);
    printf("Peeking at the first 10 elements:\n");
    for (int i = 0; i < 10; i++) {
        printf("%d: %" PRId64 "\n", i, array[i]);
    }
    printf("Sorting...\n");
    qsort(array, SIZE, sizeof(size_t), cmpfunc);
    printf("Done!\n");
    printf("Peeking at the first 10 elements:\n");
    for (int i = 0; i < 10; i++) {
        printf("%d: %" PRId64 "\n", i, array[i]);
    }

    return 0;
}