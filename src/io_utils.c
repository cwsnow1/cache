#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

#include "default_test_params.h"
#include "cache.h"
#include "io_utils.h"

extern test_params_t g_test_params;
const char params_filename[] = "./test_params.ini";

void io_utils__print_stats (cache_t *cache, uint64_t cycle) {
    if (cache->cache_level == 0) {
        printf("=========================\n");
    } else {
        printf("-------------------------\n");
    }
    printf("CACHE LEVEL %d\n", cache->cache_level);
    printf("size=%luB, block_size=%luB, associativity=%lu\n", cache->config.cache_size, cache->config.block_size, cache->config.associativity);
    float num_reads =  (float) (cache->stats.read_hits  + cache->stats.read_misses);
    float num_writes = (float) (cache->stats.write_hits + cache->stats.write_misses);
    float read_miss_rate =  (float) cache->stats.read_misses  / num_reads;
    float write_miss_rate = (float) cache->stats.write_misses / num_writes;
    float total_miss_rate = (float) (cache->stats.read_misses + cache->stats.write_misses) / (num_writes + num_reads);
    printf("Number of reads:    %08d\n", (int) num_reads);
    printf("Read miss rate:     %7.3f%%\n", 100.f * read_miss_rate);
    printf("Number of writes:   %08d\n", (int) num_writes);
    printf("Write miss rate:    %7.3f%%\n", 100.0f * write_miss_rate);
    printf("Total miss rate:    %7.3f%%\n", 100.0f * total_miss_rate);
    if (cache->cache_level == g_test_params.num_cache_levels - 1) {
        printf("-------------------------\n");
        printf("Main memory reads:  %08lu\n", cache->stats.read_misses + cache->stats.write_misses);
        printf("Main memory writes: %08lu\n\n", cache->stats.writebacks);
        printf("Total number of cycles: %010lu\n", cycle);
        cache_t *top_level_cache = cache - cache->cache_level;
        float num_reads =  (float) (top_level_cache->stats.read_hits  + top_level_cache->stats.read_misses);
        float num_writes = (float) (top_level_cache->stats.write_hits + top_level_cache->stats.write_misses);
        float cpi = (float) cycle / (num_reads + num_writes);
        printf("CPI: %.4f\n", cpi);
        printf("=========================\n\n");
    } else {
        io_utils__print_stats(cache + 1, cycle);
    }
}

/**
 * @brief Verifies the global test parameters struct is valid
 * 
 */
static void verify_test_params (void) {
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
    if (!g_test_params.min_blocks_per_set) goto verify_fail;
    line_number++;
    if (!g_test_params.max_blocks_per_set) goto verify_fail;
    line_number++;
    if (!g_test_params.max_num_threads)     goto verify_fail;

    // Check that values make sense. May help in understanding why a parameter config
    // will be found to have 0 possible cache configs
    assert(g_test_params.num_cache_levels <= MAX_NUM_CACHE_LEVELS);
    assert(g_test_params.min_block_size <= g_test_params.max_block_size);
    assert(g_test_params.min_cache_size <= g_test_params.max_cache_size);
    assert(g_test_params.min_cache_size >= g_test_params.min_block_size);
    assert(g_test_params.num_cache_levels <= MAIN_MEMORY && "Update access_time_in_cycles & enum cache_levels");
    return;

verify_fail:
    fprintf(stderr, "Error in reading %s:%d\n", params_filename, line_number);
    exit(1);
}

void io_utils__load_test_parameters (void) {
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
        fprintf(params_f, "MIN_BLOCKS_PER_SLOT=%d\n", MIN_BLOCKS_PER_SLOT);
        fprintf(params_f, "MAX_BLOCKS_PER_SLOT=%d\n", MAX_BLOCKS_PER_SLOT);
        fprintf(params_f, "MAX_NUM_THREADS=%d\n",     MAX_NUM_THREADS);
        assert(fseek(params_f, 0, SEEK_SET) == 0);
    }
    // File exists, read it in
    fscanf(params_f, "NUM_CACHE_LEVELS=%hhu\n",    &g_test_params.num_cache_levels);
    fscanf(params_f, "MIN_BLOCK_SIZE=%lu\n",       &g_test_params.min_block_size);
    fscanf(params_f, "MAX_BLOCK_SIZE=%lu\n",       &g_test_params.max_block_size);
    fscanf(params_f, "MIN_CACHE_SIZE=%lu\n",       &g_test_params.min_cache_size);
    fscanf(params_f, "MAX_CACHE_SIZE=%lu\n",       &g_test_params.max_cache_size);
    fscanf(params_f, "MIN_BLOCKS_PER_SLOT=%hhu\n", &g_test_params.min_blocks_per_set);
    fscanf(params_f, "MAX_BLOCKS_PER_SLOT=%hhu\n", &g_test_params.max_blocks_per_set);
    fscanf(params_f, "MAX_NUM_THREADS=%d\n",       &g_test_params.max_num_threads);
    fclose(params_f);
    verify_test_params();
}

uint8_t * io_utils__read_in_file (const char* filename, uint64_t *length) {
    assert(length);

    uint8_t *buffer = NULL;

    FILE* f = fopen(filename, "r");
    if (f == NULL) {
        fprintf(stderr, "Unable to open file %s\n", filename);
        goto error;
    }

    if (fseek(f, 0, SEEK_END)) {
        fprintf(stderr, "Error in file seek of %s\n", filename);
        goto error;
    }
    long m = ftell(f);
    if (m < 0) goto error;
    buffer = (uint8_t *) malloc(m);
    if (buffer == NULL)                 goto error;
    if (fseek(f, 0, SEEK_SET))          goto error;
    if (fread(buffer, 1, m, f) != m)    goto error;
    fclose(f);

    *length = (uint64_t) m;
    return buffer;

error:
    if (f) fclose(f);
    if (buffer) free(buffer);
    fprintf(stderr, "Error in reading file %s\n", filename);
    exit(1);
}

/**
 * @brief       Parses a single line of the trace file
 * 
 * @param line  Pointer within the buffer to the start of a line
 * @param addr  Out. Parsed address value
 * @param rw    Out. Access type, read or write
 */
static void parse_line (uint8_t *line, uint64_t *addr, access_t *rw) {
    line += FIRST_ADDRESS_LENGTH_IN_BYTES;
    char rw_c = *line;
    *rw = (rw_c == 'R') ? READ : WRITE;
    line += RW_LENGHT_IN_BYTES + AFTER_RW_LENGTH_IN_BYTES;
    char* end_ptr;
    *addr = strtoll((char*) line, &end_ptr, 16);
    assert(*end_ptr == '\n');
}

instruction_t * io_utils__parse_buffer (uint8_t *buffer, uint64_t length) {
    assert(buffer);
    uint8_t *buffer_start = buffer;
    uint64_t num_lines = length / FILE_LINE_LENGTH_IN_BYTES;
    instruction_t *accesses = (instruction_t*) malloc(sizeof(instruction_t) * num_lines);
    for (uint64_t i = 0; i < num_lines; i++, buffer += FILE_LINE_LENGTH_IN_BYTES) {
        parse_line(buffer, &accesses[i].ptr, &accesses[i].rw);
    }
    free(buffer_start);
    return accesses;
}
