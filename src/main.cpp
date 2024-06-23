#include <cinttypes>
#include <cstdlib>
#include <cstring>

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
        constexpr int csvFilenameMaxLength = 100;
        char csvOutputFilename[csvFilenameMaxLength];
        char* pCsvOutputFilename = csvOutputFilename + strlen(argv[2]);
        strncpy(csvOutputFilename, argv[2], csvFilenameMaxLength);
        sprintf(pCsvOutputFilename, ".csv");
        pCsvOutputStream = fopen(csvOutputFilename, "w");
    }

    Simulator* pSimulator = new Simulator(argv[1]);

    pSimulator->CreateAndRunThreads();
    pSimulator->PrintStats(pTextOutputStream, pCsvOutputStream);

    delete pSimulator;
    if (pTextOutputStream != stdout) {
        fclose(pTextOutputStream);
    }

    t = time(NULL) - t;
    printf("Program took %" PRId64 " seconds\n", t);

    return 0;
}
