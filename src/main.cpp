#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>

#include "Simulator.h"
#include "Cache.h"
#include "default_test_params.h"
#include "IOUtilities.h"
#include "SimTracer.h"
#include "debug.h"

/**
 *  @brief Prints the usage of the program in case of error
 */
static void usage (void) {
    fprintf(stderr, "Usage: ./cache <input trace> [output statistics file]\n");
    exit(1);
}

/**
 * MAIN FUNCTION
 */
int main (int argc, char** argv) {
    time_t t = time(NULL);
    FILE *stream = stdout;
    FILE *output_csv = NULL;
    if (argc < 2) {
        fprintf(stderr, "Not enough args!\n");
        usage();
    } else if (argc > 2) {
        stream = fopen(argv[2], "w");
        if (stream == NULL) {
            fprintf(stderr, "Unable to open output file %s\n", argv[2]);
            usage();
        }
        const int max_csv_filename = 100;
        char output_csv_filename[max_csv_filename];
        char *output_csv_filename_ptr = output_csv_filename + strlen(argv[2]);
        strncpy(output_csv_filename, argv[2], max_csv_filename);
        sprintf(output_csv_filename_ptr, ".csv");
        output_csv = fopen(output_csv_filename, "w");
    }

    Simulator* simulator = new Simulator(argv[1]);

    simulator->CreateAndRunThreads();
    simulator->PrintStats(stream, output_csv);

    delete simulator;
    if (stream != stdout) {
        fclose(stream);
    }

    t = time(NULL) - t;
    printf("Program took %ld seconds\n", t);

    return 0;
}
