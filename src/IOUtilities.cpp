#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "Cache.h"
#include "IOUtilities.h"
#include "debug.h"
#include "default_test_params.h"

extern TestParamaters gTestParams;
const char kParametersFilename[] = "./test_params.ini";

void IOUtilities::PrintStatistics(Memory* memory, uint64_t cycle, FILE* stream) {
    Statistics stats = memory->GetStats();
    CacheLevel cache_level = memory->GetCacheLevel();
    if (memory->GetCacheLevel() == kMainMemory) {
        return;
    }
    Cache* cache = static_cast<Cache*>(memory);
    Configuration config = cache->GetConfig();
    if (cache_level == kL1) {
        fprintf(stream, "=========================\n");
    } else {
        fprintf(stream, "-------------------------\n");
    }
    fprintf(stream, "CACHE LEVEL %d\n", cache_level);
    fprintf(stream, "size=%" PRIu64 "B, block_size=%" PRIu64 "B, associativity=%" PRIu64 "\n", config.cacheSize,
            config.blockSize, config.associativity);
    uint64_t numberOfReads = stats.readHits + stats.readMisses;
    uint64_t numberOfWrites = stats.writeHits + stats.writeMisses;
    float read_miss_rate = static_cast<float>(stats.readMisses) / numberOfReads;
    float write_miss_rate = static_cast<float>(stats.writeMisses) / numberOfWrites;
    float total_miss_rate = static_cast<float>(stats.readMisses + stats.writeMisses) / (numberOfWrites + numberOfReads);
    fprintf(stream, "Number of reads:    %08" PRIu64 "\n", stats.readHits + stats.readMisses);
    fprintf(stream, "Read miss rate:     %7.3f%%\n", 100.f * read_miss_rate);
    fprintf(stream, "Number of writes:   %08" PRIu64 "\n", stats.writeHits + stats.writeMisses);
    fprintf(stream, "Write miss rate:    %7.3f%%\n", 100.0f * write_miss_rate);
    fprintf(stream, "Total miss rate:    %7.3f%%\n", 100.0f * total_miss_rate);
    if (cache_level == gTestParams.numberOfCacheLevels - 1) {
        fprintf(stream, "-------------------------\n");
        fprintf(stream, "Main memory reads:  %08" PRIu64 "\n", stats.readMisses + stats.writeMisses);
        fprintf(stream, "Main memory writes: %08" PRIu64 "\n\n", stats.writebacks);
        fprintf(stream, "Total number of cycles: %010" PRIu64 "\n", cycle);
        Statistics topLevelStats = cache->GetTopLevelCache()->GetStats();
        float cpi = static_cast<float>(cycle) / (topLevelStats.numInstructions);
        fprintf(stream, "CPI: %.4f\n", cpi);
        fprintf(stream, "=========================\n\n");
    } else {
        PrintStatistics(memory->GetLowerCache(), cycle, stream);
    }
}

void IOUtilities::PrintStatisticsCSV(Memory* memory, uint64_t cycle, FILE* stream) {
    Statistics stats = memory->GetStats();
    CacheLevel cache_level = memory->GetCacheLevel();
    if (memory->GetCacheLevel() == kMainMemory) {
        return;
    }
    Cache* cache = static_cast<Cache*>(memory);
    Configuration config = cache->GetConfig();
    if (stream == nullptr) {
        return;
    }
    fprintf(stream, "%d,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",", cache_level, config.cacheSize, config.blockSize,
            config.associativity);
    uint64_t numberOfReads = stats.readHits + stats.readMisses;
    uint64_t numberOfWrites = stats.writeHits + stats.writeMisses;
    float read_miss_rate = static_cast<float>(stats.readMisses) / numberOfReads;
    float write_miss_rate = static_cast<float>(stats.writeMisses) / numberOfWrites;
    float total_miss_rate = static_cast<float>(stats.readMisses + stats.writeMisses) / (numberOfReads + numberOfWrites);
    fprintf(stream, "%08" PRIu64 ",%7.3f%%,%08" PRIu64 ",%7.3f%%,%7.3f%%,", numberOfWrites, 100.f * read_miss_rate,
            numberOfWrites, 100.0f * write_miss_rate, 100.0f * total_miss_rate);
    if (cache_level == gTestParams.numberOfCacheLevels - 1) {
        fprintf(stream, "%08" PRIu64 ",%08" PRIu64 ",%010" PRIu64 ",", stats.readMisses + stats.writeMisses,
                stats.writebacks, cycle);
        Statistics topLevelStats = cache->GetTopLevelCache()->GetStats();
        float cpi = static_cast<float>(cycle) / (topLevelStats.numInstructions);
        fprintf(stream, "%.4f\n", cpi);
    } else {
        PrintStatisticsCSV(memory->GetLowerCache(), cycle, stream);
    }
}

void IOUtilities::PrintConfiguration(Memory* memory, FILE* stream) {
    CacheLevel cache_level = memory->GetCacheLevel();
    if (cache_level == kMainMemory) {
        fprintf(stream, "=========================\n\n");
        return;
    }
    Cache* cache = static_cast<Cache*>(memory);
    Configuration config = cache->GetConfig();
    if (cache_level == 0) {
        fprintf(stream, "=========================\n");
    } else {
        fprintf(stream, "-------------------------\n");
    }
    fprintf(stream, "CACHE LEVEL %d\n", cache_level);
    fprintf(stream, "size=%" PRIu64 "B, block_size=%" PRIu64 "B, associativity=%" PRIu64 "\n", config.cacheSize,
            config.blockSize, config.associativity);
    PrintConfiguration(memory->GetLowerCache(), stream);
}

void IOUtilities::verify_test_params(void) {
    // Check that all values were read in correctly
    int line_number = 1;
    if (!gTestParams.numberOfCacheLevels)
        goto verify_fail;
    line_number++;
    if (!gTestParams.minBlockSize)
        goto verify_fail;
    line_number++;
    if (!gTestParams.maxBlockSize)
        goto verify_fail;
    line_number++;
    if (!gTestParams.minCacheSize)
        goto verify_fail;
    line_number++;
    if (!gTestParams.maxCacheSize)
        goto verify_fail;
    line_number++;
    if (!gTestParams.minBlocksPerSet)
        goto verify_fail;
    line_number++;
    if (!gTestParams.maxBlocksPerSet)
        goto verify_fail;
    line_number++;
    if (!gTestParams.maxNumberOfThreads)
        goto verify_fail;

    // Check that values make sense. May help in understanding why a parameter config
    // will be found to have 0 possible cache configs
    assert_release(gTestParams.numberOfCacheLevels <= kMaxNumberOfCacheLevels);
    for (int i = 0; i < kMaxNumberOfCacheLevels; i++) {
        assert_release(gTestParams.minBlockSize[i] <= gTestParams.maxBlockSize[i]);
        assert_release(gTestParams.minCacheSize[i] <= gTestParams.maxCacheSize[i]);
        assert_release(gTestParams.minCacheSize[i] >= gTestParams.minBlockSize[i]);
        assert_release(gTestParams.minBlocksPerSet[i]);
        assert_release(gTestParams.maxBlocksPerSet[i]);
        assert_release(gTestParams.maxBlocksPerSet[i] >= gTestParams.minBlocksPerSet[i]);
    }
    assert_release(gTestParams.numberOfCacheLevels <= kMaxNumberOfCacheLevels &&
                   "Update kAccessTimeInCycles & enum cache_levels");
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

void IOUtilities::LoadTestParameters(void) {
    FILE* params_f = fopen(kParametersFilename, "r");
    // If file does not exist, generate a default
    if (params_f == NULL) {
        params_f = fopen(kParametersFilename, "w+");
        assert(params_f);
        fprintf(params_f, "NUM_CACHE_LEVELS=%d\n", NUM_CACHE_LEVELS);
        for (int i = 0; i < kMaxNumberOfCacheLevels; i++) {
            fprintf(params_f, "L%d_MIN_BLOCK_SIZE=%d\n", i + 1, MIN_BLOCK_SIZE);
            fprintf(params_f, "L%d_MAX_BLOCK_SIZE=%d\n", i + 1, MAX_BLOCK_SIZE);
            fprintf(params_f, "L%d_MIN_CACHE_SIZE=%d\n", i + 1, MIN_CACHE_SIZE);
            fprintf(params_f, "L%d_MAX_CACHE_SIZE=%d\n", i + 1, MAX_CACHE_SIZE);
            fprintf(params_f, "L%d_MIN_ASSOCIATIVITY=%d\n", i + 1, MIN_ASSOCIATIVITY);
            fprintf(params_f, "L%d_MAX_ASSOCIATIVITY=%d\n", i + 1, MAX_ASSOCIATIVITY);
        }
        fprintf(params_f, "MAX_NUM_THREADS=%d\n", MAX_NUM_THREADS);
        assert_release(fseek(params_f, 0, SEEK_SET) == 0);
    }
    // File exists, read it in
    assert_release(
        fscanf(params_f, "NUM_CACHE_LEVELS=%u\n", reinterpret_cast<uint32_t*>(&gTestParams.numberOfCacheLevels)));
    for (uint64_t i = 0; i < kMaxNumberOfCacheLevels; i++) {
        int cacheLevel;
        int expectedCacheLevel = static_cast<int>(i) + 1;
        assert_release(fscanf(params_f, "L%d_MIN_BLOCK_SIZE=%" PRIu64 "\n", &cacheLevel, &gTestParams.minBlockSize[i]));
        assert_release(fscanf(params_f, "L%d_MAX_BLOCK_SIZE=%" PRIu64 "\n", &cacheLevel, &gTestParams.maxBlockSize[i]));
        assert_release(fscanf(params_f, "L%d_MIN_CACHE_SIZE=%" PRIu64 "\n", &cacheLevel, &gTestParams.minCacheSize[i]));
        assert_release(fscanf(params_f, "L%d_MAX_CACHE_SIZE=%" PRIu64 "\n", &cacheLevel, &gTestParams.maxCacheSize[i]));
        uint32_t associativity;
        assert_release(fscanf(params_f, "L%d_MIN_ASSOCIATIVITY=%u\n", &cacheLevel, &associativity));
        gTestParams.minBlocksPerSet[i] = static_cast<uint8_t>(associativity);
        assert_release(fscanf(params_f, "L%d_MAX_ASSOCIATIVITY=%u\n", &cacheLevel, &associativity));
        gTestParams.maxBlocksPerSet[i] = static_cast<uint8_t>(associativity);
        assert_release(cacheLevel == expectedCacheLevel);
    }
    assert_release(fscanf(params_f, "MAX_NUM_THREADS=%d\n", &gTestParams.maxNumberOfThreads));
    fclose(params_f);
    verify_test_params();
}

uint8_t* IOUtilities::ReadInFile(const char* filename, uint64_t& length) {

    uint8_t* buffer = NULL;
    uint64_t m;

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
    if (m < 0)
        goto error;
    buffer = new uint8_t[m];
    if (buffer == NULL)
        goto error;
    if (fseek(f, 0, SEEK_SET))
        goto error;
    if (fread(buffer, 1, m, f) != m)
        goto error;
    fclose(f);

    length = m;
    return buffer;

error:
    if (f)
        fclose(f);
    delete[] buffer;
    fprintf(stderr, "Error in reading file %s\n", filename);
    exit(1);
}

void IOUtilities::parseLine(uint8_t* line, std::vector<Instruction>& dataAccesses,
                            std::vector<Instruction>& instructionAccesses) {
    line += kPaddingLengthInBytes;
    char* end_ptr;
    instructionAccesses.push_back(Instruction(strtoull(reinterpret_cast<char*>(line), &end_ptr, 16), READ));
    /*
    pInstructionAccess->rw = READ;
    pInstructionAccess->ptr = strtoull(reinterpret_cast<char*> (line), &end_ptr, 16);
    */
    line += kPaddingLengthInBytes + kAddressLengthInBytes;
    char rw_c = *line;
    line += kRwLengthInBytes + kPaddingAfterRwLengthInBytes;
    Instruction dataAccess;
    if (rw_c == 'R')
        dataAccess.rw = READ;
    else if (rw_c == 'W')
        dataAccess.rw = WRITE;
    else
        return;
    dataAccess.ptr = strtoll(reinterpret_cast<char*>(line), &end_ptr, 16);
    instructionAccesses.back().dataAccessIndex = dataAccesses.size();
    dataAccesses.push_back(dataAccess);
    // If this assert fails, it is likely that the addresses in the trace are not uniform
    // and do not match the length assumptions made here
    assert(*end_ptr == '\n');
}

void IOUtilities::ParseBuffer(uint8_t* buffer, uint64_t length, MemoryAccesses& accesses) {
    assert(buffer);
    const uint8_t* buffer_start = buffer;
    uint64_t numberOfLines = length / kFileLineLengthInBytes;
    for (uint64_t i = 0; i < numberOfLines; i++, buffer += kFileLineLengthInBytes) {
        IOUtilities::parseLine(buffer, accesses.dataAccesses_, accesses.instructionAccesses_);
    }
    delete[] buffer_start;
}
