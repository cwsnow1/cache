#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

#include "cache.h"
#include "default_test_params.h"

#define MAX(x, y)   (x > y ? x : y)

// Common across all threads
instruction_t *accesses;
uint64_t num_accesses;
pthread_t *threads;
cache_t **g_caches;
uint64_t num_configs = 0;
// Note: No performance difference is seen by limiting the
// number of outstanding threads. The only benefit would be
// memory savings when running with large numbers of configs
static volatile int32_t threads_outstanding;

test_params_t g_test_params;
const char params_filename[] = "./test_params.ini";

/**
 * @brief Prints the gathered statistics for a given run
 * 
 */
static void print_stats (cache_t *cache) {
    if (cache->cache_level == 0) {
        printf("=========================\n");
    }
    printf("CACHE LEVEL %d\n", cache->cache_level);
    printf("size=%lu, block_size=%lu, num_blocks_per_slot=%lu\n", cache->cache_size, cache->block_size, cache->num_blocks_per_slot);
    float miss_rate = (float)(((float)cache->stats.read_misses + (float)cache->stats.write_misses) / ((float)cache->stats.read_hits + (float)cache->stats.write_hits + (float)cache->stats.read_misses + (float)cache->stats.write_misses));
    printf("Miss Rate:      %7.3f%%\n", 100.0f * miss_rate);
    if (cache->cache_level == g_test_params.num_cache_levels - 1) {
        printf("=========================\n\n");
    } else {
        print_stats(cache + 1);
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
    if (!g_test_params.min_blocks_per_slot) goto verify_fail;
    line_number++;
    if (!g_test_params.max_blocks_per_slot) goto verify_fail;
    line_number++;
    if (!g_test_params.max_num_threads)     goto verify_fail;

    // Check that values make sense. May help in understanding why a parameter config
    // will be found to have 0 possible cache configs
    assert(g_test_params.num_cache_levels <= MAX_NUM_CACHE_LEVELS);
    assert(g_test_params.min_block_size <= g_test_params.max_block_size);
    assert(g_test_params.min_cache_size <= g_test_params.max_cache_size);
    assert(g_test_params.min_cache_size >= g_test_params.min_block_size);
    return;

verify_fail:
    fprintf(stderr, "Error in reading %s:%d\n", params_filename, line_number);
    exit(1);
}

/**
 * @brief Loads test_params.ini if extant, creates it otherwise
 * 
 */
static void load_test_parameters (void) {
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
    fscanf(params_f, "MIN_BLOCKS_PER_SLOT=%hhu\n", &g_test_params.min_blocks_per_slot);
    fscanf(params_f, "MAX_BLOCKS_PER_SLOT=%hhu\n", &g_test_params.max_blocks_per_slot);
    fscanf(params_f, "MAX_NUM_THREADS=%d\n",       &g_test_params.max_num_threads);
    fclose(params_f);
    verify_test_params();
}

/**
 *  @brief                  Takes in a trace file
 * 
 *  @param filename         Name of the trace file to read
 *  @param num_accesses     Output. Returns the length of the file in bytes
 *  @return                 Array of file contents
 */
static uint8_t * read_in_file (const char* filename, uint64_t *length) {
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
 * Trace file format is as follows:
 * 0xdeadbeefdead: W 0xbeefdeadbeef\n
 * 0xbeefdeadbeef: R 0xdeadbeefdead\n
 * etc.
 * 
 * The first address is a don't care, that is 16 bytes including WS before rw
 * 1 byte rw
 * 3 bytes dc including WS
 * 12 bytes for address
 * 1 byte newline
 */
#define FILE_LINE_LENGTH_IN_BYTES           (33)
#define FIRST_ADDRESS_LENGTH_IN_BYTES       (16)
#define RW_LENGHT_IN_BYTES                  (1)
#define AFTER_RW_LENGTH_IN_BYTES            (3)

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

/**
 * @brief           Parses the contents of a trace file and coverts to internal structure array
 * 
 * @param buffer    Pointer to the contents of the file
 * @param length    Lenght of buffer in bytes
 * @return          Array of instruction structs for internal use
 */
static instruction_t * parse_buffer (uint8_t *buffer, uint64_t length) {
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

/**
 *  @brief Prints the usage of the program in case of error
 */
static void usage (void) {
    fprintf(stderr, "Please provide a trace file\n");
    exit(1);
}

/**
 * @brief               Runs through all memory accesses with given setup cache
 * 
 * @param L1_cache      Top level cache pointer, assumed to be initialized
 */
void * sim_cache (void *L1_cache) {
    cache_t *this_cache = (cache_t *) L1_cache;
    assert(this_cache->cache_level == 0);
    for (uint64_t i = 0; i < num_accesses; i++) {
        cache__handle_access(this_cache, accesses[i]);
    }
    threads_outstanding--;
    pthread_exit(NULL);
}

/**
 * @brief Generate threads that will call sim_cache
 * 
 */
static void create_and_run_threads (void) {
    for (uint64_t i = 0; i < num_configs; i++) {
        while (threads_outstanding == g_test_params.max_num_threads)
            ;
        if (pthread_create(&threads[i], NULL, sim_cache, (void*) &g_caches[i][0])) {
            fprintf(stderr, "Error in creating thread %lu\n", i);
        }
        threads_outstanding++;
    }
    for (uint64_t i = 0; i < num_configs; i++) {
        pthread_join(threads[i], NULL);
    }
    assert(threads_outstanding == 0);
}

/**
 *  @brief Recursive function to tell all cache configs
 * 
 *  @param caches           array of caches
 *  @param cache_level      the level of the cache this function will try to init
 *  @param min_block_size   minimum block size this cache level will try to init
 *  @param min_cache_size   minimum cache size this cache level will try to init
 */
static void setup_caches (uint8_t cache_level, uint64_t min_block_size, uint64_t min_cache_size) {
    static uint64_t thread_num = 0;
    static config_t configs[MAX_NUM_CACHE_LEVELS];
    for (uint64_t block_size = min_block_size; block_size <= g_test_params.max_block_size; block_size <<= 1) {
        for (uint64_t cache_size = MAX(min_cache_size, block_size); cache_size <= g_test_params.max_cache_size; cache_size <<= 1) {
            for (uint8_t blocks_per_slot = g_test_params.min_blocks_per_slot; blocks_per_slot <= g_test_params.max_blocks_per_slot; blocks_per_slot <<= 1) {
                configs[cache_level].block_size = block_size;
                configs[cache_level].cache_size = cache_size;
                configs[cache_level].num_blocks_per_slot = blocks_per_slot;
                if (cache__is_cache_config_valid(configs[cache_level])) {
                    if (cache_level < g_test_params.num_cache_levels - 1) {
                        setup_caches(cache_level + 1, block_size, cache_size);
                    } else {
                        assert(cache__init(&g_caches[thread_num][0], 0, configs, thread_num));
                        thread_num++;
                    }
                }
            }
        }
    }
}

/**
 * @brief                   Recursively alculate the total number of valid cache configs
 * 
 * @param num_configs       Out. Tracks the total number of valid configs
 * @param cache_level       Level of cache the function is in, 0 (L1) when called from without
 * @param min_block_size    Minimum block size as specfied in cache_params.h
 * @param min_cache_size    Minimum cache size as specfied in cache_params.h
 */
static void calculate_num_valid_configs (uint64_t *num_configs, uint8_t cache_level, uint64_t min_block_size, uint64_t min_cache_size) {
    for (uint64_t block_size = min_block_size; block_size <= g_test_params.max_block_size; block_size <<= 1) {
        for (uint64_t cache_size = MAX(min_cache_size, block_size); cache_size <= g_test_params.max_cache_size; cache_size <<= 1) {
            for (uint8_t blocks_per_slot = g_test_params.min_blocks_per_slot; blocks_per_slot <= g_test_params.max_blocks_per_slot; blocks_per_slot <<= 1) {
                config_t config = {
                    .block_size = block_size,
                    .cache_size = cache_size,
                    .num_blocks_per_slot = blocks_per_slot,
                };
                if (cache__is_cache_config_valid(config)) {
                    if (cache_level < g_test_params.num_cache_levels - 1) {
                        calculate_num_valid_configs(num_configs, cache_level + 1, block_size, cache_size);
                    } else {
                        (*num_configs)++;
                    }
                }
            }
        }
    }
}

/**
 * MAIN FUNCTION
 */
int main (int argc, char** argv) {
    time_t t = time(NULL);
    if (argc < 2) {
        fprintf(stderr, "Not enough args!\n");
        usage();
    }

    // Look for test parameters file and generate a default if not found
    load_test_parameters();

    // Read in trace file
    uint64_t file_length = 0;
    uint8_t *file_contents = read_in_file(argv[1], &file_length);
    accesses = parse_buffer(file_contents, file_length);

    assert(accesses != NULL);
    num_accesses = file_length / FILE_LINE_LENGTH_IN_BYTES;

    calculate_num_valid_configs(&num_configs, 0, g_test_params.min_block_size, g_test_params.min_cache_size);
    printf("Total number of possible configs = %lu\n", num_configs);
    threads_outstanding = 0;
    threads = (pthread_t*) malloc(sizeof(pthread_t) * num_configs);
    assert(threads);
    g_caches = (cache_t**) malloc(sizeof(cache_t *) * num_configs);
    assert(g_caches);
    for (uint64_t i = 0; i < num_configs; i++) {
        g_caches[i] = (cache_t*) malloc(sizeof(cache_t) * g_test_params.num_cache_levels);
        assert(g_caches[i]);
    }
    setup_caches(0, g_test_params.min_block_size, g_test_params.min_cache_size);
    create_and_run_threads();
    for (uint64_t i = 0; i < num_configs; i++) {
        print_stats(g_caches[i]);
        cache__reset(g_caches[i]);
    }

    free(accesses);
    for (uint64_t i = 0; i < num_configs; i++) {
        free(g_caches[i]);
    }
    free(g_caches);
    free(threads);
    t = time(NULL) - t;
    printf("Program took %ld seconds\n", t);
    return 0;
}
