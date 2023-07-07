#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "default_test_params.h"
#include "Cache.h"
#include "IOUtilities.h"
#include "debug.h"

extern TestParamaters gTestParams;
const char kParametersFilename[] = "./test_params.ini";

void IOUtilities::PrintStatistics (Memory *memory, uint64_t cycle, FILE *stream) {
    Statistics stats = memory->GetStats();
    CacheLevel cache_level = memory->GetCacheLevel();
    if (memory->GetCacheLevel() == kMainMemory) {
        return;
    }
    Cache *cache = static_cast<Cache*>(memory);
    Configuration config = cache->GetConfig();
    if (cache_level == kL1) {
        fprintf(stream, "=========================\n");
    } else {
        fprintf(stream, "-------------------------\n");
    }
    fprintf(stream, "CACHE LEVEL %d\n", cache_level);
    fprintf(stream, "size=%luB, block_size=%luB, associativity=%lu\n", config.cacheSize, config.blockSize, config.associativity);
    float num_reads =  (float) (stats.readHits  + stats.readMisses);
    float num_writes = (float) (stats.writeHits + stats.writeMisses);
    float read_miss_rate =  (float) stats.readMisses  / num_reads;
    float write_miss_rate = (float) stats.writeMisses / num_writes;
    float total_miss_rate = (float) (stats.readMisses + stats.writeMisses) / (num_writes + num_reads);
    fprintf(stream, "Number of reads:    %08d\n", (int) num_reads);
    fprintf(stream, "Read miss rate:     %7.3f%%\n", 100.f * read_miss_rate);
    fprintf(stream, "Number of writes:   %08d\n", (int) num_writes);
    fprintf(stream, "Write miss rate:    %7.3f%%\n", 100.0f * write_miss_rate);
    fprintf(stream, "Total miss rate:    %7.3f%%\n", 100.0f * total_miss_rate);
    if (cache_level == gTestParams.numberOfCacheLevels - 1) {
        fprintf(stream, "-------------------------\n");
        fprintf(stream, "Main memory reads:  %08lu\n", stats.readMisses + stats.writeMisses);
        fprintf(stream, "Main memory writes: %08lu\n\n", stats.writebacks);
        fprintf(stream, "Total number of cycles: %010lu\n", cycle);
        Statistics topLevelStats = cache->GetTopLevelCache()->GetStats();
        float num_reads =  (float) (topLevelStats.readHits  + topLevelStats.readMisses);
        float num_writes = (float) (topLevelStats.writeHits + topLevelStats.writeMisses);
        float cpi = (float) cycle / (num_reads + num_writes);
        fprintf(stream, "CPI: %.4f\n", cpi);
        fprintf(stream, "=========================\n\n");
    } else {
        PrintStatistics(memory->GetLowerCache(), cycle, stream);
    }
}

void IOUtilities::PrintStatisticsCSV (Memory *memory, uint64_t cycle, FILE *stream) {
    Statistics stats = memory->GetStats();
    CacheLevel cache_level = memory->GetCacheLevel();
    if (memory->GetCacheLevel() == kMainMemory) {
        return;
    }
    Cache *cache = static_cast<Cache*>(memory);
    Configuration config = cache->GetConfig();
    if (stream == nullptr) {
        return;
    }
    fprintf(stream, "%d,%lu,%lu,%lu,", cache_level, config.cacheSize, config.blockSize, config.associativity);
    float num_reads =  (float) (stats.readHits  + stats.readMisses);
    float num_writes = (float) (stats.writeHits + stats.writeMisses);
    float read_miss_rate =  (float) stats.readMisses  / num_reads;
    float write_miss_rate = (float) stats.writeMisses / num_writes;
    float total_miss_rate = (float) (stats.readMisses + stats.writeMisses) / (num_writes + num_reads);
    fprintf(stream, "%08d,%7.3f%%,%08d,%7.3f%%,%7.3f%%,", (int) num_reads, 100.f * read_miss_rate, (int) num_writes, 100.0f * write_miss_rate, 100.0f * total_miss_rate);
    if (cache_level == gTestParams.numberOfCacheLevels - 1) {
        fprintf(stream, "%08lu,%08lu,%010lu,", stats.readMisses + stats.writeMisses, stats.writebacks, cycle);
        Statistics topLevelStats = cache->GetTopLevelCache()->GetStats();
        float num_reads =  (float) (topLevelStats.readHits  + topLevelStats.readMisses);
        float num_writes = (float) (topLevelStats.writeHits + topLevelStats.writeMisses);
        float cpi = (float) cycle / (num_reads + num_writes);
        fprintf(stream, "%.4f\n", cpi);
    } else {
        PrintStatisticsCSV(memory->GetLowerCache(), cycle, stream);
    }
}

void IOUtilities::PrintConfiguration (Memory *memory, FILE *stream) {
    CacheLevel cache_level = memory->GetCacheLevel();
    if (cache_level == kMainMemory) {
        fprintf(stream, "=========================\n\n");
        return;
    }
    Cache *cache = static_cast<Cache*>(memory);
    Configuration config = cache->GetConfig();
    if (cache_level == 0) {
        fprintf(stream, "=========================\n");
    } else {
        fprintf(stream, "-------------------------\n");
    }
    fprintf(stream, "CACHE LEVEL %d\n", cache_level);
    fprintf(stream, "size=%luB, block_size=%luB, associativity=%lu\n", config.cacheSize, config.blockSize, config.associativity);
    PrintConfiguration(memory->GetLowerCache(), stream);
}

void IOUtilities::verify_test_params (void) {
    // Check that all values were read in correctly
    int line_number = 1;
    if (!gTestParams.numberOfCacheLevels)    goto verify_fail;
    line_number++;
    if (!gTestParams.minBlockSize)      goto verify_fail;
    line_number++;
    if (!gTestParams.maxBlockSize)      goto verify_fail;
    line_number++;
    if (!gTestParams.minCacheSize)      goto verify_fail;
    line_number++;
    if (!gTestParams.maxCacheSize)      goto verify_fail;
    line_number++;
    if (!gTestParams.minBlocksPerSet)  goto verify_fail;
    line_number++;
    if (!gTestParams.maxBlocksPerSet)  goto verify_fail;
    line_number++;
    if (!gTestParams.maxNumberOfThreads)     goto verify_fail;

    // Check that values make sense. May help in understanding why a parameter config
    // will be found to have 0 possible cache configs
    assert_release(gTestParams.numberOfCacheLevels <= MAX_NUM_CACHE_LEVELS);
    assert_release(gTestParams.minBlockSize <= gTestParams.maxBlockSize);
    assert_release(gTestParams.minCacheSize <= gTestParams.maxCacheSize);
    assert_release(gTestParams.minCacheSize >= gTestParams.minBlockSize);
    assert_release(gTestParams.numberOfCacheLevels <= kMainMemory && "Update kAccessTimeInCycles & enum cache_levels");
#if (CONSOLE_PRINT == 1)
    if (gTestParams.maxNumberOfThreads > 1) {
        printf("WARNING: Console printing with multiple threads is not recommended. Do you wish to continue? [Y/n]\n");
        char ret = 'n';
        assert_release(scanf("%c", &ret) == 1);
        if (ret != 'Y') {
            exit(0);
        }
    }
#endif
    return;

verify_fail:
    fprintf(stderr, "Error in reading %s:%d\n", kParametersFilename, line_number);
    exit(1);
}

void IOUtilities::LoadTestParameters (void) {
    FILE *params_f = fopen(kParametersFilename, "r");
    // If file does not exist, generate a default
    if (params_f == NULL) {
        params_f = fopen(kParametersFilename, "w+");
        assert(params_f);
        fprintf(params_f, "NUM_CACHE_LEVELS=%d\n",    NUM_CACHE_LEVELS);
        fprintf(params_f, "MIN_BLOCK_SIZE=%d\n",      MIN_BLOCK_SIZE);
        fprintf(params_f, "MAX_BLOCK_SIZE=%d\n",      MAX_BLOCK_SIZE);
        fprintf(params_f, "MIN_CACHE_SIZE=%d\n",      MIN_CACHE_SIZE);
        fprintf(params_f, "MAX_CACHE_SIZE=%d\n",      MAX_CACHE_SIZE);
        fprintf(params_f, "MIN_ASSOCIATIVITY=%d\n",   MIN_ASSOCIATIVITY);
        fprintf(params_f, "MAX_ASSOCIATIVITY=%d\n",   MAX_ASSOCIATIVITY);
        fprintf(params_f, "MAX_NUM_THREADS=%d\n",     MAX_NUM_THREADS);
        assert_release(fseek(params_f, 0, SEEK_SET) == 0);
    }
    // File exists, read it in
    assert_release(fscanf(params_f, "NUM_CACHE_LEVELS=%hhu\n",    &gTestParams.numberOfCacheLevels));
    assert_release(fscanf(params_f, "MIN_BLOCK_SIZE=%lu\n",       &gTestParams.minBlockSize));
    assert_release(fscanf(params_f, "MAX_BLOCK_SIZE=%lu\n",       &gTestParams.maxBlockSize));
    assert_release(fscanf(params_f, "MIN_CACHE_SIZE=%lu\n",       &gTestParams.minCacheSize));
    assert_release(fscanf(params_f, "MAX_CACHE_SIZE=%lu\n",       &gTestParams.maxCacheSize));
    assert_release(fscanf(params_f, "MIN_ASSOCIATIVITY=%hhu\n",   &gTestParams.minBlocksPerSet));
    assert_release(fscanf(params_f, "MAX_ASSOCIATIVITY=%hhu\n",   &gTestParams.maxBlocksPerSet));
    assert_release(fscanf(params_f, "MAX_NUM_THREADS=%d\n",       &gTestParams.maxNumberOfThreads));
    fclose(params_f);
    verify_test_params();
}

uint8_t * IOUtilities::ReadInFile (const char* filename, uint64_t *length) {
    assert(length);

    uint8_t *buffer = NULL;
    size_t m;

    FILE* f = fopen(filename, "r");
    if (f == NULL) {
        fprintf(stderr, "Unable to open file %s\n", filename);
        goto error;
    }

    if (fseek(f, 0, SEEK_END)) {
        fprintf(stderr, "Error in file seek of %s\n", filename);
        goto error;
    }
    m = ftell(f);
    if (m < 0) goto error;
    buffer = new uint8_t[m];
    if (buffer == NULL)                 goto error;
    if (fseek(f, 0, SEEK_SET))          goto error;
    if (fread(buffer, 1, m, f) != m)    goto error;
    fclose(f);

    *length = (uint64_t) m;
    return buffer;

error:
    if (f) fclose(f);
    delete[] buffer;
    fprintf(stderr, "Error in reading file %s\n", filename);
    exit(1);
}

void IOUtilities::parseLine (uint8_t *line, uint64_t *address, access_t *rw) {
    line += FIRST_ADDRESS_LENGTH_IN_BYTES;
    char rw_c = *line;
    *rw = (rw_c == 'R') ? READ : WRITE;
    line += RW_LENGHT_IN_BYTES + AFTER_RW_LENGTH_IN_BYTES;
    char* end_ptr;
    *address = strtoll((char*) line, &end_ptr, 16);
    assert(*end_ptr == '\n');
}

Instruction * IOUtilities::ParseBuffer (uint8_t *buffer, uint64_t length) {
    assert(buffer);
    const uint8_t *buffer_start = buffer;
    uint64_t num_lines = length / FILE_LINE_LENGTH_IN_BYTES;
    Instruction *accesses = new Instruction[num_lines];
    for (uint64_t i = 0; i < num_lines; i++, buffer += FILE_LINE_LENGTH_IN_BYTES) {
        IOUtilities::parseLine(buffer, &accesses[i].ptr, &accesses[i].rw);
    }
    delete[] buffer_start;
    return accesses;
}
