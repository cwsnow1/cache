#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#include "cache.h"
#include "default_test_params.h"
#include "io_utils.h"
#include "sim_trace.h"

#define MAX(x, y)   (x > y ? x : y)

// Common across all threads
instruction_t *accesses;
uint64_t num_accesses;
pthread_t *threads;
cache_t **g_caches;
uint64_t num_configs = 0;
// Note: No performance benefit is seen by limiting the
// number of outstanding threads. The only benefit is
// memory savings & keeping the computer usable when
// running with large numbers of configs
volatile static int32_t threads_outstanding;
volatile static uint64_t configs_to_test;
static pthread_mutex_t lock;

test_params_t g_test_params;

/**
 *  @brief Prints the usage of the program in case of error
 */
static void usage (void) {
    fprintf(stderr, "Please provide a trace file\n");
    exit(1);
}

/**
 * @brief Prints program progress every second
 * 
 */
void * track_progress(void * empty) {
    printf("Running... %02.0f%% complete\n", 0.0f);
    while(configs_to_test) {
        float configs_done = (float) (num_configs - configs_to_test);
        float progress_percent = (configs_done / (float) num_configs) * 100.0f;
        printf("\x1b[1A");
        printf("Running... %d threads running, %lu to go. %02.0f%% complete\n", threads_outstanding, configs_to_test, progress_percent);
        sleep(1);
    }
    pthread_exit(NULL);
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
    pthread_mutex_lock(&lock);
    configs_to_test--;
    threads_outstanding--;
    pthread_mutex_unlock(&lock);
    pthread_exit(NULL);
}

/**
 * @brief Generate threads that will call sim_cache
 * 
 */
static void create_and_run_threads (void) {
    if (pthread_mutex_init(&lock, NULL) != 0){
        fprintf(stderr, "Mutex lock init failed\n");
        exit(1);
    }
    pthread_t progress_thread;
    pthread_create(&progress_thread, NULL, track_progress, NULL);
    for (uint64_t i = 0; i < num_configs; i++) {
        while (threads_outstanding == g_test_params.max_num_threads)
            ;
        pthread_mutex_lock(&lock);
        threads_outstanding++;
        pthread_mutex_unlock(&lock);
        if (pthread_create(&threads[i], NULL, sim_cache, (void*) &g_caches[i][0])) {
            fprintf(stderr, "Error in creating thread %lu\n", i);
        }
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
        for (uint64_t cache_size = MAX(min_cache_size, block_size); cache_size <= (g_test_params.max_cache_size * (1 << cache_level)); cache_size <<= 1) {
            for (uint8_t blocks_per_slot = g_test_params.min_blocks_per_slot; blocks_per_slot <= g_test_params.max_blocks_per_slot; blocks_per_slot <<= 1) {
                configs[cache_level].block_size = block_size;
                configs[cache_level].cache_size = cache_size;
                configs[cache_level].num_blocks_per_slot = blocks_per_slot;
                if (cache__is_cache_config_valid(configs[cache_level])) {
                    if (cache_level < g_test_params.num_cache_levels - 1) {
                        setup_caches(cache_level + 1, block_size, cache_size * 2);
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
    io_utils__load_test_parameters();

    // Read in trace file
    uint64_t file_length = 0;
    uint8_t *file_contents = io_utils__read_in_file(argv[1], &file_length);
    accesses = io_utils__parse_buffer(file_contents, file_length);

    assert(accesses != NULL);
    num_accesses = file_length / FILE_LINE_LENGTH_IN_BYTES;

    calculate_num_valid_configs(&num_configs, 0, g_test_params.min_block_size, g_test_params.min_cache_size);
    printf("Total number of possible configs = %lu\n", num_configs);
    configs_to_test = num_configs;
#ifdef SIM_TRACE
    int ret = sim_trace__init();
    if (ret < 0) {
        exit(0);
    }
#endif
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
#ifdef SIM_TRACE
    sim_trace__write_to_file_and_exit(SIM_TRACE_FILENAME);
#endif
    for (uint64_t i = 0; i < num_configs; i++) {
        io_utils__print_stats(g_caches[i]);
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
#ifdef SIM_TRACE
    printf("Wrote sim trace output to %s\n", SIM_TRACE_FILENAME);
#endif
    return 0;
}
