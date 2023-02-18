#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

#include "cache.h"
#include "sim_trace.h"
#ifdef SIM_TRACE

static uint32_t *thread_indices;
static sim_trace_entry_t **sim_trace_buffer;
extern uint64_t num_configs;
extern test_params_t g_test_params;
extern cache_t **g_caches;

FILE * sim_trace__init(const char *filename) {
    if (num_configs > SIM_TRACE_WARNING_THRESHOLD) {
        printf("The number of configs is very high for simulation tracing.\n");
        printf("There is no issue with that, but it will take ~2 times as long\n");
        printf("as normal, and will write a .bin file that will be %lu MiB\n\n", (num_configs * SIM_TRACE_BUFFER_SIZE_IN_BYTES) >> 20);
        printf("Note: On my setup, making the number of threads unlimited has a\n");
        printf("bigger benefit to performance when sim tracing than when not.\n\n");
        printf("Do you wish to continue? [Y/n]\n");
        char response;
        scanf("%c", &response);
        if (response != 'Y') {
            return NULL;
        }
    }
    assert(num_configs);

    FILE *f = fopen(filename, "wb");
    if (f == NULL) {
        fprintf(stderr, "Error in opening sim trace file\n");
        exit(1);
    }
    size_t ret = 0;
    // uint32_t number of entries in each sim_trace
    const uint32_t sim_trace_buffer_size_in_entries =  SIM_TRACE_BUFFER_SIZE_IN_ENTRIES;
    ret = fwrite(&sim_trace_buffer_size_in_entries, sizeof(uint32_t), 1, f);
    assert(ret == 1);
    // uint16_t number of configs
    ret = fwrite(&num_configs, sizeof(uint16_t), 1, f);
    assert(ret == 1);
    assert(fwrite(&g_test_params.num_cache_levels, sizeof(uint8_t), 1, f) == 1);

    assert(SIM_TRACE_BUFFER_SIZE_IN_BYTES <= UINT32_MAX);
    thread_indices = (uint32_t*) malloc(sizeof(uint32_t) * g_test_params.max_num_threads);
    memset(thread_indices, 0, sizeof(uint32_t) * g_test_params.max_num_threads);
    sim_trace_buffer = (sim_trace_entry_t**) malloc(sizeof(sim_trace_entry_t*) * g_test_params.max_num_threads);
    for (uint64_t i = 0; i < g_test_params.max_num_threads; i++) {
        sim_trace_buffer[i] = (sim_trace_entry_t*) malloc(SIM_TRACE_BUFFER_SIZE_IN_BYTES);
        memset(sim_trace_buffer[i], SIM_TRACE__INVALID, SIM_TRACE_BUFFER_SIZE_IN_BYTES);
    }
    return f;
}

void sim_trace__print(trace_entry_id_t trace_entry_id, cache_t *cache, uint64_t *values) {
    uint64_t thread_id = cache->thread_id;
    assert(thread_id < g_test_params.max_num_threads);
    uint64_t cycle = cache->cycle;
    uint8_t cache_level = cache->cache_level;
    sim_trace_buffer[thread_id][thread_indices[thread_id]].trace_entry_id = trace_entry_id;
    sim_trace_buffer[thread_id][thread_indices[thread_id]].cache_level = cache_level;
    sim_trace_buffer[thread_id][thread_indices[thread_id]].cycle = cycle;
    if (values != NULL) {
        memcpy(&sim_trace_buffer[thread_id][thread_indices[thread_id]].values[0], values, sizeof(uint64_t) * MAX_NUM_SIM_TRACE_VALUES);
    }
    // Roll over when buffer is filled
    if (++thread_indices[thread_id] == SIM_TRACE_BUFFER_SIZE_IN_ENTRIES) {
        thread_indices[thread_id] = 0;
    }
}

/**
 * File format is as follows:
 * uint32_t number of entries in each sim_trace
 * uint16_t number of configs
 * uint8_t num_cache_levels
 * Config entries
 * 
 * Config entry is as follows:
 * uint32_t oldest entry index
 * config of each cache
 * buffer
 */

void sim_trace__write_thread_buffer(cache_t *cache, FILE *f) {
    size_t ret = 0;
    uint64_t thread_id = cache->thread_id;

    uint32_t oldest_index = thread_indices[thread_id];
    ret = fwrite(&oldest_index, sizeof(uint32_t), 1, f);
    assert(ret == 1);
    // configs of each cache
    config_t *configs = (config_t*) malloc(sizeof(config_t) * g_test_params.num_cache_levels);
    uint8_t i = 0;
    for (cache_t *cache_i = cache; cache_i->cache_level != MAIN_MEMORY; cache_i = cache_i->lower_cache, i++) {
        configs[i].cache_size = cache_i->config.cache_size;
        configs[i].block_size = cache_i->config.block_size;
        configs[i].associativity = cache_i->config.associativity;
    }
    assert(fwrite(configs, sizeof(config_t), g_test_params.num_cache_levels, f) == g_test_params.num_cache_levels);
    free(configs);
    ret = fwrite(sim_trace_buffer[thread_id], sizeof(sim_trace_entry_t), SIM_TRACE_BUFFER_SIZE_IN_ENTRIES, f);
    assert(ret == SIM_TRACE_BUFFER_SIZE_IN_ENTRIES);

    // Reset this thread's buffer for next config to use
    thread_indices[thread_id] = 0;
    memset(sim_trace_buffer[thread_id], SIM_TRACE__INVALID, SIM_TRACE_BUFFER_SIZE_IN_BYTES);
}

void sim_trace__reset(FILE *f) {
    for (uint16_t i = 0; i < g_test_params.max_num_threads; i++) {
        free(sim_trace_buffer[i]);
    }
    free(sim_trace_buffer);
    fclose(f);
}

#endif
