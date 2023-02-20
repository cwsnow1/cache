#include <stdio.h>
#include <stdint.h>

#define TRACE_LENGTH (0x100000)

int main (int argc, char **argv) {
    FILE *f = fopen("seq.trace", "w");
    for (uint64_t i = 0; i < TRACE_LENGTH; ++i) {
        fprintf(f, "0x000000000000: R 0x%012lx\n", i);
    }
    return 0;
}
