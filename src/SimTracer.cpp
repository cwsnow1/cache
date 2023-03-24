#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

#include "Cache.h"
#include "SimTracer.h"
#include "debug.h"
#if (SIM_TRACE == 1)

#include "sim_trace_decoder.h"

extern test_params_t g_test_params;

SimTracer::SimTracer(const char *filename, uint64_t num_configs) {
    if (num_configs > SIM_TRACE_WARNING_THRESHOLD) {
        printf("The number of configs is very high for simulation tracing.\n");
        printf("There is no issue with that, but it will take ~2 times as long\n");
        printf("as normal, and will write a .bin file that will be %lu MiB\n\n", (num_configs * kSimTraceBufferSizeInBytes) >> 20);
        printf("Note: On my setup, making the number of threads unlimited has a\n");
        printf("bigger benefit to performance when sim tracing than when not.\n\n");
        printf("Do you wish to continue? [Y/n]\n");
        char response;
        scanf("%c", &response);
        if (response != 'Y') {
            exit(0);
        }
    }
    assert(num_configs);

    f_ = fopen(filename, "wb");
    if (f_ == NULL) {
        fprintf(stderr, "Error in opening sim trace file\n");
        exit(1);
    }
    CODE_FOR_ASSERT(size_t ret = 0);
    // uint32_t number of bytes in each sim_trace
    const uint32_t sim_trace_buffer_size_in_bytes =  kSimTraceBufferSizeInBytes;
    CODE_FOR_ASSERT(ret =) fwrite(&sim_trace_buffer_size_in_bytes, sizeof(uint32_t), 1, f_);
    assert(ret == 1);
    // uint16_t number of configs
    CODE_FOR_ASSERT(ret =) fwrite(&num_configs, sizeof(uint16_t), 1, f_);
    assert(ret == 1);
    CODE_FOR_ASSERT(ret =) fwrite(&g_test_params.num_cache_levels, sizeof(uint8_t), 1, f_);
    assert(ret == 1);

    assert(kSimTraceBufferSizeInBytes <= UINT32_MAX);
    buffer_append_points_ = new uint8_t*[g_test_params.max_num_threads];
    sim_trace_buffer_ = new uint8_t*[g_test_params.max_num_threads];
    entry_counters_ = new uint64_t[g_test_params.max_num_threads]();
    previous_cycle_counter_ = new uint64_t[g_test_params.max_num_threads]();
    for (uint64_t i = 0; i < static_cast<uint64_t>(g_test_params.max_num_threads); i++) {
        sim_trace_buffer_[i] = new uint8_t[kSimTraceBufferSizeInBytes]();
        buffer_append_points_[i] = sim_trace_buffer_[i];
        assert(sim_trace_buffer_[i]);
        memset(sim_trace_buffer_[i], SIM_TRACE__INVALID, kSimTraceBufferSizeInBytes);
    }
}

SimTracer::~SimTracer() {
    for (uint16_t i = 0; i < g_test_params.max_num_threads; i++) {
        delete[] sim_trace_buffer_[i];
    }
    delete[] buffer_append_points_;
    delete[] entry_counters_;
    delete[] previous_cycle_counter_;
    delete[] sim_trace_buffer_;
    fclose(f_);
}

void SimTracer::Print(trace_entry_id_t trace_entry_id, Cache *cache, ...) {
    uint64_t thread_id = cache->thread_id_;
    assert(thread_id < static_cast<uint64_t>(g_test_params.max_num_threads));
    uint64_t cycle = cache->GetCycle();
    CacheLevel cache_level = cache->GetCacheLevel();
    // Roll over when buffer is filled
    uint8_t *last_entry_ptr = sim_trace_buffer_[thread_id] + SIM_TRACE_LAST_ENTRY_OFFSET;
    if (buffer_append_points_[thread_id] >= last_entry_ptr) {
        buffer_append_points_[thread_id] = sim_trace_buffer_[thread_id];
    }
    // Sync pattern if needed
    if (++entry_counters_[thread_id] == SIM_TRACE_SYNC_INTERVAL) {
        *((sync_pattern_t*)buffer_append_points_[thread_id]) = sync_pattern;
        buffer_append_points_[thread_id] += sizeof(sync_pattern_t);
        entry_counters_[thread_id] = 0;
    }
    if (cycle - previous_cycle_counter_[thread_id] > UINT16_MAX) {
        fprintf(stderr, "cycle offset overflow!\n");
    }
    uint16_t cycle_offset = (uint8_t)(cycle - previous_cycle_counter_[thread_id]);
    previous_cycle_counter_[thread_id] = cycle;
    SimTraceEntry entry = SimTraceEntry(cycle_offset, trace_entry_id, cache_level);
    *((SimTraceEntry*) buffer_append_points_[thread_id]) = entry;
    buffer_append_points_[thread_id] += sizeof(SimTraceEntry);
    va_list values;
    va_start(values, cache);
    for (uint8_t i = 0; i < sim_trace_entry_num_arguments[trace_entry_id]; i++) {
        *((sim_trace_entry_data_t*)buffer_append_points_[thread_id]) = va_arg(values, sim_trace_entry_data_t);
        buffer_append_points_[thread_id] += sizeof(sim_trace_entry_data_t);
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

void SimTracer::WriteThreadBuffer(Cache *cache) {
    CODE_FOR_ASSERT(size_t ret = 0);
    uint64_t thread_id = cache->thread_id_;

    uint32_t buffer_append_point_offset = (uint32_t)(buffer_append_points_[thread_id] - sim_trace_buffer_[thread_id]);
    CODE_FOR_ASSERT(ret =) fwrite(&buffer_append_point_offset, sizeof(uint32_t), 1, f_);
    assert(ret == 1);
    // configs of each cache
    Configuration *configs = new Configuration[g_test_params.num_cache_levels];
    uint8_t i = 0;
    for (Cache *cache_i = cache; cache_i->GetCacheLevel() != kMainMemory; cache_i = cache_i->GetLowerCache(), i++) {
        configs[i].cache_size = cache_i->GetConfig().cache_size;
        configs[i].block_size = cache_i->GetConfig().block_size;
        configs[i].associativity = cache_i->GetConfig().associativity;
    }
    CODE_FOR_ASSERT(ret =) fwrite(configs, sizeof(Configuration), g_test_params.num_cache_levels, f_);
    assert(ret == g_test_params.num_cache_levels);
    delete[] configs;
    CODE_FOR_ASSERT(ret =) fwrite(sim_trace_buffer_[thread_id], sizeof(uint8_t), kSimTraceBufferSizeInBytes, f_);
    assert(ret == kSimTraceBufferSizeInBytes);

    // Reset this thread's buffer for next config to use
    buffer_append_points_[thread_id] = sim_trace_buffer_[thread_id];
    entry_counters_[thread_id] = 0;
    previous_cycle_counter_[thread_id] = 0;
    memset(sim_trace_buffer_[thread_id], SIM_TRACE__INVALID, kSimTraceBufferSizeInBytes);
}

#endif
