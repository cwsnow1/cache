#include <cinttypes>
#include <cstdlib>
#include <string>

#ifdef __GNUC__
#include <unistd.h>
#endif

#ifdef _MSC_VER
#include <time.h>
#endif

#include "Cache.h"
#include "IOUtilities.h"
#include "SimTracer.h"
#include "Simulator.h"
#include "debug.h"
#include "default_test_params.h"
/**
 *  @brief Prints the usage of the program in case of error
 */
static void usage(void) {
    fprintf(stderr, "Usage: ./cache <input trace> [output statistics file]\n");
    exit(1);
}

/**
 * MAIN FUNCTION
 */
int main(int argc, char** argv) {
    time_t t = time(NULL);
    FILE* pTextOutputStream = stdout;
    FILE* pCsvOutputStream = nullptr;
    if (argc < 2) {
        fprintf(stderr, "Not enough args!\n");
        usage();
    } else if (argc > 2) {
        pTextOutputStream = fopen(argv[2], "w");
        if (pTextOutputStream == nullptr) {
            fprintf(stderr, "Unable to open output file %s\n", argv[2]);
            usage();
        }
        std::string csvOutputFilename(argv[2]);
        csvOutputFilename.append(".csv");
        pCsvOutputStream = fopen(csvOutputFilename.c_str(), "w");
    }

    Simulator simulator(argv[1]);

    simulator.CreateAndRunThreads();
    simulator.PrintStats(pTextOutputStream, pCsvOutputStream);

    if (pTextOutputStream != stdout) {
        fclose(pTextOutputStream);
    }

    t = time(NULL) - t;
    printf("Program took %" PRId64 " seconds\n", t);

    return 0;
}
