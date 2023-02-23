#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>

#include "cache.h"
#include "default_test_params.h"
#include "io_utils.h"
#include "debug.h"

#define MAX(x, y)   (x > y ? x : y)
typedef uint64_t bitfield64_t;
#define set_bit(bitfield, index)    (bitfield) |= (1 << (index))
#define reset_bit(bitfield, index)  (bitfield) &= ~(1 << (index))

// Common across all threads
instruction_t *accesses;
uint64_t num_accesses;
pthread_t *threads;
cache_t **g_caches;
static uint64_t *cycle_counter;
uint64_t num_configs = 0;
// Note: No performance benefit is seen by limiting the
// number of outstanding threads. The only benefit is
// memory savings & keeping the computer usable when
// running with large numbers of configs
static int32_t num_threads_outstanding;
static uint64_t configs_to_test;
static pthread_t *threads_outstanding;
#define INVALID_THREAD_ID   (UINT64_MAX)
static pthread_mutex_t lock;

test_params_t g_test_params;

#if (SIM_TRACE == 1)
FILE *sim_trace_f;
#endif

/**
 *  @brief Prints the usage of the program in case of error
 */
static void usage (void) {
    fprintf(stderr, "Usage: ./cache <input trace> [output statistics file]\n");
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
        printf("Running... %d threads running, %lu to go. %02.0f%% complete\n", num_threads_outstanding, configs_to_test, progress_percent);
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
    assert(this_cache->cache_level == L1);
    cache__allocate_memory(this_cache);
    uint64_t config_index = (this_cache - g_caches[0]) / g_test_params.num_cache_levels;
    cycle_counter[config_index] = 0;
    bitfield64_t oustanding_requests = 0;
    int16_t completed_requests[MAX_NUM_REQUESTS] = { 0 };
    assert(MAX_NUM_REQUESTS <= 64 && "Too many requests to store requests in this bitfield");
    uint64_t i = 0;
    bool work_done = false;
    do {
#if (CONSOLE_PRINT == 1)
        printf("====================\nTICK %010lu\n====================\n", cycle_counter[config_index]);
        char c;
        scanf("%c", &c);
#endif
        work_done = false;
        if (i < num_accesses) {
            int16_t request_index = cache__add_access_request(this_cache, accesses[i], cycle_counter[config_index]);
            if (request_index != -1) {
                work_done = true;
                ++i;
                set_bit(oustanding_requests, request_index);
            }
        }
        uint64_t num_completed_requests = cache__process_cache(this_cache, cycle_counter[config_index], completed_requests);
        work_done |= this_cache->work_done_this_cycle;
        for (uint64_t j = 0; j < num_completed_requests; j++) {
            work_done = true;
            reset_bit(oustanding_requests, completed_requests[j]);
        }
        if (work_done) {
            cycle_counter[config_index]++;
        } else {
            uint64_t earliest_next_useful_cycle = UINT64_MAX;
            for (cache_t *cache_i = this_cache; cache_i != NULL; cache_i = cache_i->lower_cache) {
                if (cache_i->earliest_next_useful_cycle < earliest_next_useful_cycle) {
                    earliest_next_useful_cycle = cache_i->earliest_next_useful_cycle;
                }
            }
            assert(earliest_next_useful_cycle > cycle_counter[config_index]);
            if (earliest_next_useful_cycle < UINT64_MAX && !this_cache->work_done_this_cycle) {
#if (CONSOLE_PRINT == 1)
                printf("Skipping to earliest next useful cycle = %lu\n", earliest_next_useful_cycle);
#endif
                cycle_counter[config_index] = earliest_next_useful_cycle;
            } else {
                cycle_counter[config_index]++;
            }
        }
    } while (oustanding_requests || i < num_accesses);
    pthread_mutex_lock(&lock);
#if (SIM_TRACE == 1)
    sim_trace__write_thread_buffer(this_cache, sim_trace_f);
#endif
    configs_to_test--;
    num_threads_outstanding--;
    assert(threads_outstanding[this_cache->thread_id] == pthread_self());
    // Mark thread as not in use
    threads_outstanding[this_cache->thread_id] = INVALID_THREAD_ID;
    pthread_mutex_unlock(&lock);
    cache__free_memory(this_cache);
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
#if (CONSOLE_PRINT == 0)
    pthread_t progress_thread;
    pthread_create(&progress_thread, NULL, track_progress, NULL);
#endif
    uint64_t thread_id = 0;

    // internal thread_id to pthread thread_id mapping used to track which threads are active
    threads_outstanding = (pthread_t*) malloc(sizeof(pthread_t) * g_test_params.max_num_threads);
    memset(threads_outstanding, (int) INVALID_THREAD_ID, sizeof(pthread_t) * g_test_params.max_num_threads);
    for (uint64_t i = 0; i < num_configs; i++) {
        while (num_threads_outstanding == g_test_params.max_num_threads)
            ;
        pthread_mutex_lock(&lock);
        num_threads_outstanding++;
        // Search for a thread_id that is not in use
        for (thread_id = 0; thread_id < g_test_params.max_num_threads; thread_id++) {
            if (threads_outstanding[thread_id] == INVALID_THREAD_ID) {
                break;
            }
        }
        cache__set_thread_id(&g_caches[i][L1], thread_id);
        if (pthread_create(&threads[i], NULL, sim_cache, (void*) &g_caches[i][L1])) {
            fprintf(stderr, "Error in creating thread %lu\n", i);
        }
        threads_outstanding[thread_id] = threads[i];
        pthread_mutex_unlock(&lock);
    }
    for (uint64_t i = 0; i < num_configs; i++) {
        pthread_join(threads[i], NULL);
    }
#if (CONSOLE_PRINT == 0)
    pthread_join(progress_thread, NULL);
#endif
    assert(num_threads_outstanding == 0);
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
            for (uint8_t blocks_per_set = g_test_params.min_blocks_per_set; blocks_per_set <= g_test_params.max_blocks_per_set; blocks_per_set <<= 1) {
                configs[cache_level].block_size = block_size;
                configs[cache_level].cache_size = cache_size;
                configs[cache_level].associativity = blocks_per_set;
                if (cache__is_cache_config_valid(configs[cache_level])) {
                    if (cache_level < g_test_params.num_cache_levels - 1) {
                        setup_caches(cache_level + 1, block_size, cache_size * 2);
                    } else {
                        CODE_FOR_ASSERT(bool ret =) cache__init(&g_caches[thread_num][0], 0, g_test_params.num_cache_levels, configs);
                        assert(ret);
                        thread_num++;
                    }
                }
            }
        }
    }
}

/**
 * @brief                   Recursively calculate the total number of valid cache configs
 * 
 * @param num_configs       Out. Tracks the total number of valid configs
 * @param cache_level       Level of cache the function is in, 0 (L1) when called from without
 * @param min_block_size    Minimum block size as specfied in cache_params.h
 * @param min_cache_size    Minimum cache size as specfied in cache_params.h
 */
static void calculate_num_valid_configs (uint64_t *num_configs, uint8_t cache_level, uint64_t min_block_size, uint64_t min_cache_size) {
    for (uint64_t block_size = min_block_size; block_size <= g_test_params.max_block_size; block_size <<= 1) {
        for (uint64_t cache_size = MAX(min_cache_size, block_size); cache_size <= g_test_params.max_cache_size; cache_size <<= 1) {
            for (uint8_t blocks_per_set = g_test_params.min_blocks_per_set; blocks_per_set <= g_test_params.max_blocks_per_set; blocks_per_set <<= 1) {
                config_t config = {
                    .block_size = block_size,
                    .cache_size = cache_size,
                    .associativity = blocks_per_set,
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
    FILE *stream = stdout;
    FILE *output_csv = NULL;
    if (argc > 2) {
        stream = fopen(argv[2], "w");
        if (stream == NULL) {
            fprintf(stderr, "Unable to open output file %s\n", argv[2]);
            usage();
        }
        char output_csv_filename[100];
        char *output_csv_filename_ptr = output_csv_filename + strlen(argv[2]);
        strcpy(output_csv_filename, argv[2]);
        sprintf(output_csv_filename_ptr, ".csv");
        output_csv = fopen(output_csv_filename, "w");
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
    if (num_configs < g_test_params.max_num_threads || (g_test_params.max_num_threads < 0)) {
        g_test_params.max_num_threads = num_configs;
    }
    configs_to_test = num_configs;
#if (SIM_TRACE == 1)
    uint64_t sim_buffer_memory_size = g_test_params.max_num_threads * (uint64_t) SIM_TRACE_BUFFER_SIZE_IN_BYTES;
    if (sim_buffer_memory_size > MEMORY_USAGE_LIMIT) {
        const int32_t new_max_num_threads = MEMORY_USAGE_LIMIT / (uint64_t)SIM_TRACE_BUFFER_SIZE_IN_BYTES;
        printf("Sim trace buffer memory is too big for %d threads. Lower thread count to %d\n", g_test_params.max_num_threads, new_max_num_threads);
        g_test_params.max_num_threads = new_max_num_threads;
    }
    sim_trace_f = sim_trace__init(SIM_TRACE_FILENAME);
    if (sim_trace_f == NULL) {
        exit(0);
    }
#endif
    cycle_counter = (uint64_t*) malloc(sizeof(uint64_t) * num_configs);
    assert(cycle_counter);
    memset(cycle_counter, 0, sizeof(uint64_t) * num_configs);
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
    float min_cpi = (float) cycle_counter[0];
    uint64_t min_i = 0;
    if (output_csv) {
        for (int i = 0; i < g_test_params.num_cache_levels; i++) {
            fprintf(output_csv, "Cache level, Cache size, Block size, Associativity, Num reads, Read miss rate, Num writes, Write miss rate, Total miss rate,");
        }
        fprintf(output_csv, "Main memory reads, Main memory writes, Total number of cycles, CPI\n");
    }
    for (uint64_t i = 0; i < num_configs; i++) {
        io_utils__print_stats(g_caches[i], cycle_counter[i], stream);
        io_utils__print_stats_csv(g_caches[i], cycle_counter[i], output_csv);
        float num_reads =  (float) (g_caches[i][0].stats.read_hits  + g_caches[i][0].stats.read_misses);
        float num_writes = (float) (g_caches[i][0].stats.write_hits + g_caches[i][0].stats.write_misses);
        float cpi = (float) cycle_counter[i] / (num_reads + num_writes);
        if (cpi < min_cpi) {
            if (i != 0) {
                // Previous "minimum" CPI cache wasn't reset, do so now
                cache__reset(g_caches[min_i]);
            }
            min_cpi = cpi;
            min_i = i;
        } else {
            cache__reset(g_caches[i]);
        }
    }
    if (output_csv) {
        fclose(output_csv);
    }
    fprintf(stream, "The config with the lowest CPI of %.4f:\n", min_cpi);
    io_utils__print_config(g_caches[min_i], stream);
    cache__reset(g_caches[min_i]);
    if (stream != stdout) {
        fclose(stream);
    }
    free(cycle_counter);
    free(accesses);
    for (uint64_t i = 0; i < num_configs; i++) {
        free(g_caches[i]);
    }
    free(g_caches);
    free(threads);
    t = time(NULL) - t;
    printf("Program took %ld seconds\n", t);
#if (SIM_TRACE == 1)
    sim_trace__reset(sim_trace_f);
    printf("Wrote sim trace output to %s\n", SIM_TRACE_FILENAME);
#endif
    return 0;
}
