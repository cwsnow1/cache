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

extern test_params_t g_test_params;
const char params_filename[] = "./test_params.ini";

void IOUtilities::PrintStatistics (Cache *cache, uint64_t cycle, FILE *stream) {
    Statistics stats = cache->GetStats();
    CacheLevel cache_level = cache->GetCacheLevel();
    Configuration config = cache->GetConfig();
    if (cache_level == 0) {
        fprintf(stream, "=========================\n");
    } else {
        fprintf(stream, "-------------------------\n");
    }
    fprintf(stream, "CACHE LEVEL %d\n", cache_level);
    fprintf(stream, "size=%luB, block_size=%luB, associativity=%lu\n", config.cache_size, config.block_size, config.associativity);
    float num_reads =  (float) (stats.read_hits  + stats.read_misses);
    float num_writes = (float) (stats.write_hits + stats.write_misses);
    float read_miss_rate =  (float) stats.read_misses  / num_reads;
    float write_miss_rate = (float) stats.write_misses / num_writes;
    float total_miss_rate = (float) (stats.read_misses + stats.write_misses) / (num_writes + num_reads);
    fprintf(stream, "Number of reads:    %08d\n", (int) num_reads);
    fprintf(stream, "Read miss rate:     %7.3f%%\n", 100.f * read_miss_rate);
    fprintf(stream, "Number of writes:   %08d\n", (int) num_writes);
    fprintf(stream, "Write miss rate:    %7.3f%%\n", 100.0f * write_miss_rate);
    fprintf(stream, "Total miss rate:    %7.3f%%\n", 100.0f * total_miss_rate);
    if (cache_level == g_test_params.num_cache_levels - 1) {
        fprintf(stream, "-------------------------\n");
        fprintf(stream, "Main memory reads:  %08lu\n", stats.read_misses + stats.write_misses);
        fprintf(stream, "Main memory writes: %08lu\n\n", stats.writebacks);
        fprintf(stream, "Total number of cycles: %010lu\n", cycle);
        Statistics topLevelStats = cache->GetTopLevelCache()->GetStats();
        float num_reads =  (float) (topLevelStats.read_hits  + topLevelStats.read_misses);
        float num_writes = (float) (topLevelStats.write_hits + topLevelStats.write_misses);
        float cpi = (float) cycle / (num_reads + num_writes);
        fprintf(stream, "CPI: %.4f\n", cpi);
        fprintf(stream, "=========================\n\n");
    } else {
        PrintStatistics(cache->GetLowerCache(), cycle, stream);
    }
}

void IOUtilities::PrintStatisticsCSV (Cache *cache, uint64_t cycle, FILE *stream) {
    Statistics stats = cache->GetStats();
    CacheLevel cache_level = cache->GetCacheLevel();
    Configuration config = cache->GetConfig();
    if (stream == NULL) {
        return;
    }
    fprintf(stream, "%d,%lu,%lu,%lu,", cache_level, config.cache_size, config.block_size, config.associativity);
    float num_reads =  (float) (stats.read_hits  + stats.read_misses);
    float num_writes = (float) (stats.write_hits + stats.write_misses);
    float read_miss_rate =  (float) stats.read_misses  / num_reads;
    float write_miss_rate = (float) stats.write_misses / num_writes;
    float total_miss_rate = (float) (stats.read_misses + stats.write_misses) / (num_writes + num_reads);
    fprintf(stream, "%08d,%7.3f%%,%08d,%7.3f%%,%7.3f%%,", (int) num_reads, 100.f * read_miss_rate, (int) num_writes, 100.0f * write_miss_rate, 100.0f * total_miss_rate);
    if (cache_level == g_test_params.num_cache_levels - 1) {
        fprintf(stream, "%08lu,%08lu,%010lu,", stats.read_misses + stats.write_misses, stats.writebacks, cycle);
        Statistics topLevelStats = cache->GetTopLevelCache()->GetStats();
        float num_reads =  (float) (topLevelStats.read_hits  + topLevelStats.read_misses);
        float num_writes = (float) (topLevelStats.write_hits + topLevelStats.write_misses);
        float cpi = (float) cycle / (num_reads + num_writes);
        fprintf(stream, "%.4f\n", cpi);
    } else {
        PrintStatisticsCSV(cache->GetLowerCache(), cycle, stream);
    }
}

void IOUtilities::PrintConfiguration (Cache *cache, FILE *stream) {
    CacheLevel cache_level = cache->GetCacheLevel();
    Configuration config = cache->GetConfig();
    if (cache_level == 0) {
        fprintf(stream, "=========================\n");
    } else {
        fprintf(stream, "-------------------------\n");
    }
    fprintf(stream, "CACHE LEVEL %d\n", cache_level);
    fprintf(stream, "size=%luB, block_size=%luB, associativity=%lu\n", config.cache_size, config.block_size, config.associativity);
    if (cache_level != g_test_params.num_cache_levels - 1) {
        PrintConfiguration(cache->GetLowerCache(), stream);
    } else {
        fprintf(stream, "=========================\n\n");
    }
}

void IOUtilities::verify_test_params (void) {
    // Check that all values were read in correctly
    int line_number = 1;
    if (!g_test_params.num_cache_levels)    goto verify_fail;
    line_number++;
    if (!g_test_params.min_block_size)      goto verify_fail;
    line_number++;
    if (!g_test_params.max_block_size)      goto verify_fail;
    line_number++;
    if (!g_test_params.min_cache_size)      goto verify_fail;
    line_number++;
    if (!g_test_params.max_cache_size)      goto verify_fail;
    line_number++;
    if (!g_test_params.min_blocks_per_set)  goto verify_fail;
    line_number++;
    if (!g_test_params.max_blocks_per_set)  goto verify_fail;
    line_number++;
    if (!g_test_params.max_num_threads)     goto verify_fail;

    // Check that values make sense. May help in understanding why a parameter config
    // will be found to have 0 possible cache configs
    assert_release(g_test_params.num_cache_levels <= MAX_NUM_CACHE_LEVELS);
    assert_release(g_test_params.min_block_size <= g_test_params.max_block_size);
    assert_release(g_test_params.min_cache_size <= g_test_params.max_cache_size);
    assert_release(g_test_params.min_cache_size >= g_test_params.min_block_size);
    assert_release(g_test_params.num_cache_levels <= kMainMemory && "Update access_time_in_cycles & enum cache_levels");
#if (CONSOLE_PRINT == 1)
    if (g_test_params.max_num_threads > 1) {
        printf("WARNING: Console printing with multiple threads is not recommended. Do you wish to continue? [Y/n]\n");
        char ret = 'n';
        scanf("%c", &ret);
        if (ret != 'Y') {
            exit(0);
        }
    }
#endif
    return;

verify_fail:
    fprintf(stderr, "Error in reading %s:%d\n", params_filename, line_number);
    exit(1);
}

void IOUtilities::LoadTestParameters (void) {
    FILE *params_f = fopen(params_filename, "r");
    // If file does not exist, generate a default
    if (params_f == NULL) {
        params_f = fopen(params_filename, "w+");
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
    assert_release(fscanf(params_f, "NUM_CACHE_LEVELS=%hhu\n",    &g_test_params.num_cache_levels));
    assert_release(fscanf(params_f, "MIN_BLOCK_SIZE=%lu\n",       &g_test_params.min_block_size));
    assert_release(fscanf(params_f, "MAX_BLOCK_SIZE=%lu\n",       &g_test_params.max_block_size));
    assert_release(fscanf(params_f, "MIN_CACHE_SIZE=%lu\n",       &g_test_params.min_cache_size));
    assert_release(fscanf(params_f, "MAX_CACHE_SIZE=%lu\n",       &g_test_params.max_cache_size));
    assert_release(fscanf(params_f, "MIN_ASSOCIATIVITY=%hhu\n",   &g_test_params.min_blocks_per_set));
    assert_release(fscanf(params_f, "MAX_ASSOCIATIVITY=%hhu\n",   &g_test_params.max_blocks_per_set));
    assert_release(fscanf(params_f, "MAX_NUM_THREADS=%d\n",       &g_test_params.max_num_threads));
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
