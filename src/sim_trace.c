#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

#include "cache.h"
#include "sim_trace.h"
#if (SIM_TRACE == 1)

#include "sim_trace_decoder.h"

static uint8_t **buffer_append_points;
static uint8_t **sim_trace_buffer;
static uint16_t *entry_counters;
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
    // uint32_t number of bytes in each sim_trace
    const uint32_t sim_trace_buffer_size_in_bytes =  SIM_TRACE_BUFFER_SIZE_IN_BYTES;
    ret = fwrite(&sim_trace_buffer_size_in_bytes, sizeof(uint32_t), 1, f);
    assert(ret == 1);
    // uint16_t number of configs
    ret = fwrite(&num_configs, sizeof(uint16_t), 1, f);
    assert(ret == 1);
    assert(fwrite(&g_test_params.num_cache_levels, sizeof(uint8_t), 1, f) == 1);

    assert(SIM_TRACE_BUFFER_SIZE_IN_BYTES <= UINT32_MAX);
    buffer_append_points = (uint8_t**) malloc(sizeof(uint8_t*) * g_test_params.max_num_threads);
    sim_trace_buffer = (uint8_t**) malloc(sizeof(uint8_t*) * g_test_params.max_num_threads);
    entry_counters = (uint16_t*) malloc(sizeof(uint16_t) * g_test_params.max_num_threads);
    memset(entry_counters, 0, sizeof(uint16_t) * g_test_params.max_num_threads);
    for (uint64_t i = 0; i < g_test_params.max_num_threads; i++) {
        sim_trace_buffer[i] = (uint8_t*) malloc(SIM_TRACE_BUFFER_SIZE_IN_BYTES);
        buffer_append_points[i] = sim_trace_buffer[i];
        assert(sim_trace_buffer[i]);
        memset(sim_trace_buffer[i], SIM_TRACE__INVALID, SIM_TRACE_BUFFER_SIZE_IN_BYTES);
    }
    return f;
}

void sim_trace__print(trace_entry_id_t trace_entry_id, cache_t *cache, ...) {
    uint64_t thread_id = cache->thread_id;
    assert(thread_id < g_test_params.max_num_threads);
    uint64_t cycle = cache->cycle;
    uint8_t cache_level = cache->cache_level;
    // Roll over when buffer is filled
    uint8_t *last_entry_ptr = sim_trace_buffer[thread_id] + SIM_TRACE_LAST_ENTRY_OFFSET;
    if (buffer_append_points[thread_id] >= last_entry_ptr) {
        buffer_append_points[thread_id] = sim_trace_buffer[thread_id];
    }
    // Sync pattern if needed
    if (++entry_counters[thread_id] == SIM_TRACE_SYNC_INTERVAL) {
        *((sync_pattern_t*)buffer_append_points[thread_id]) = sync_pattern;
        buffer_append_points[thread_id] += sizeof(sync_pattern_t);
        entry_counters[thread_id] = 0;
    }
    sim_trace_entry_t entry = {
        .trace_entry_id = trace_entry_id,
        .cache_level = cache_level,
        .cycle = cycle,
    };
    *((sim_trace_entry_t*) buffer_append_points[thread_id]) = entry;
    buffer_append_points[thread_id] += sizeof(sim_trace_entry_t);
    va_list values;
    va_start(values, cache);
    for (uint8_t i = 0; i < sim_trace_entry_num_arguments[trace_entry_id]; i++) {
        *((sim_trace_entry_data_t*)buffer_append_points[thread_id]) = va_arg(values, sim_trace_entry_data_t);
        buffer_append_points[thread_id] += sizeof(sim_trace_entry_data_t);
    }
    va_end(values);
}

/**
 * File format is as follows:
 * uint32_t buffer size in bytes
 * uint16_t number of configs
 * uint8_t num_cache_levels
 * Config entries
 * 
 * Config entry is as follows:
 * uint32_t buffer append point offset
 * config of each cache
 * buffer
 */

void sim_trace__write_thread_buffer(cache_t *cache, FILE *f) {
    size_t ret = 0;
    uint64_t thread_id = cache->thread_id;

    uint32_t buffer_append_point_offset = (uint32_t)(buffer_append_points[thread_id] - sim_trace_buffer[thread_id]);
    ret = fwrite(&buffer_append_point_offset, sizeof(uint32_t), 1, f);
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
    ret = fwrite(sim_trace_buffer[thread_id], sizeof(uint8_t), SIM_TRACE_BUFFER_SIZE_IN_BYTES, f);
    assert(ret == SIM_TRACE_BUFFER_SIZE_IN_BYTES);

    // Reset this thread's buffer for next config to use
    buffer_append_points[thread_id] = sim_trace_buffer[thread_id];
    entry_counters[thread_id] = 0;
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
