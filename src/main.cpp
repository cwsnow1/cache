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
#include "test_params.h"

#define MAX(x, y)   (x > y ? x : y)
typedef uint64_t bitfield64_t;
#define set_bit(bitfield, index)    (bitfield) |= (1 << (index))
#define reset_bit(bitfield, index)  (bitfield) &= ~(1 << (index))

// Common across all threads
Instruction *accesses;
uint64_t num_accesses;
pthread_t *threads;
static uint64_t *cycle_counter;
uint64_t num_configs = 0;
// Note: No performance benefit is seen by limiting the
// number of outstanding threads. The only benefit is
// memory savings & keeping the computer usable when
// running with large numbers of configs
volatile static int32_t threads_outstanding;
volatile static uint64_t configs_to_test;
static pthread_mutex_t lock;

TestParams *g_test_params;

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
    Cache *this_cache = static_cast<Cache*>(L1_cache);
    assert(this_cache->getCacheLevel() == 0);
    uint64_t thread_id = this_cache->getThreadID();
    cycle_counter[thread_id] = 0;
    bitfield64_t oustanding_requests = 0;
    int16_t completed_requests[MAX_NUM_REQUESTS] = { 0 };
    static_assert(MAX_NUM_REQUESTS <= 64 && "Too many requests to store requests in this bitfield");
    uint64_t i = 0;
    do {
#ifdef CONSOLE_PRINT
        printf("====================\nTICK %010lu\n====================\n", cycle_counter[thread_id]);
        //char c;
        //assert(scanf("%c", &c));
#endif
        if (i < num_accesses) {
            int16_t request_index = this_cache->addAccessRequest(accesses[i], cycle_counter[thread_id]);
            if (request_index != -1) {
                ++i;
                set_bit(oustanding_requests, request_index);
            }
        }
        uint64_t num_completed_requests = this_cache->processCache(cycle_counter[thread_id], completed_requests);
        for (uint64_t j = 0; j < num_completed_requests; j++) {
            reset_bit(oustanding_requests, completed_requests[j]);
        }
        cycle_counter[thread_id]++;
    } while (oustanding_requests || i < num_accesses);
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
static void create_and_run_threads (Cache **caches) {
    if (pthread_mutex_init(&lock, NULL) != 0){
        fprintf(stderr, "Mutex lock init failed\n");
        exit(1);
    }
#ifndef CONSOLE_PRINT
    pthread_t progress_thread;
    pthread_create(&progress_thread, NULL, track_progress, NULL);
#endif
    for (uint64_t i = 0; i < num_configs; i++) {
        while (threads_outstanding == g_test_params->getMaxNumThreads())
            ;
        pthread_mutex_lock(&lock);
        threads_outstanding++;
        pthread_mutex_unlock(&lock);
        if (pthread_create(&threads[i], NULL, sim_cache, (void*) caches[i])) {
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
static Cache **setup_caches (uint64_t num_configs, uint8_t cache_level, uint64_t min_block_size, uint64_t min_cache_size) {
    static Cache **caches = NULL;
    if (caches == NULL) {
        caches = new Cache*[num_configs];
    }
    static uint64_t thread_num = 0;
    static Config configs[MAX_NUM_CACHE_LEVELS];
    for (uint64_t block_size = min_block_size; block_size <= g_test_params->getMaxBlockSize(); block_size <<= 1) {
        for (uint64_t cache_size = MAX(min_cache_size, block_size); cache_size <= (g_test_params->getMaxCacheSize() * (1 << cache_level)); cache_size <<= 1) {
            for (uint8_t blocks_per_set = g_test_params->getMinAssociativity(); blocks_per_set <= g_test_params->getMaxAssociativity(); blocks_per_set <<= 1) {
                configs[cache_level].block_size = block_size;
                configs[cache_level].cache_size = cache_size;
                configs[cache_level].associativity = blocks_per_set;
                configs[cache_level].num_cache_levels = g_test_params->getNumCacheLevels();
                if (Cache::isCacheConfigValid(configs[cache_level])) {
                    if (cache_level < g_test_params->getNumCacheLevels() - 1) {
                        setup_caches(num_configs, cache_level + 1, block_size, cache_size * 2);
                    } else {
                        caches[thread_num] = new Cache(NULL, 0, configs, thread_num);
                        ++thread_num;
                    }
                }
            }
        }
    }
    return caches;
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
    for (uint64_t block_size = min_block_size; block_size <= g_test_params->getMaxBlockSize(); block_size <<= 1) {
        for (uint64_t cache_size = MAX(min_cache_size, block_size); cache_size <= g_test_params->getMaxCacheSize(); cache_size <<= 1) {
            for (uint8_t blocks_per_set = g_test_params->getMinAssociativity(); blocks_per_set <= g_test_params->getMaxAssociativity(); blocks_per_set <<= 1) {
                Config config;
                config.cache_size = cache_size;
                config.block_size = block_size;
                config.associativity = blocks_per_set;
                if (Cache::isCacheConfigValid(config)) {
                    if (cache_level < g_test_params->getNumCacheLevels() - 1) {
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
    g_test_params = io_utils::loadTestParameters();

    // Read in trace file
    uint64_t file_length = 0;
    uint8_t *file_contents = io_utils::readInFile(argv[1], &file_length);
    accesses = io_utils::parseBuffer(file_contents, file_length);

    assert(accesses != NULL);
    num_accesses = file_length / FILE_LINE_LENGTH_IN_BYTES;

    calculate_num_valid_configs(&num_configs, 0, g_test_params->getMinBlockSize(), g_test_params->getMinCacheSize());
    printf("Total number of possible configs = %lu\n", num_configs);
    configs_to_test = num_configs;
#ifdef SIM_TRACE
    int ret = sim_trace__init();
    if (ret < 0) {
        exit(0);
    }
#endif
    cycle_counter = new uint64_t[num_configs];
    memset(cycle_counter, 0, sizeof(uint64_t) * num_configs);
    threads = new pthread_t[num_configs];

    Cache** caches = setup_caches(num_configs, 0, g_test_params->getMinBlockSize(), g_test_params->getMinCacheSize());
    create_and_run_threads(caches);
#ifdef SIM_TRACE
    sim_trace__write_to_file_and_exit(SIM_TRACE_FILENAME);
#endif
    for (uint64_t i = 0; i < num_configs; i++) {
        caches[i]->printStats(cycle_counter[i]);
        caches[i]->~Cache();
    }
    delete cycle_counter;
    delete accesses;
    delete caches;
    delete threads;
    t = time(NULL) - t;
    printf("Program took %ld seconds\n", t);
#ifdef SIM_TRACE
    printf("Wrote sim trace output to %s\n", SIM_TRACE_FILENAME);
#endif
    return 0;
}
