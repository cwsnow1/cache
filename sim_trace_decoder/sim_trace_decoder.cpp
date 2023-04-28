#include <stdio.h>
#include <stdint.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#if (SIM_TRACE == 0)
#undef SIM_TRACE
#define SIM_TRACE (1)
#endif

#include "Cache.h"
#include "SimTracer.h"
#include "sim_trace_decoder.h"

#define MAX_NUM_THREADS (MEMORY_USAGE_LIMIT / (uint64_t)kSimTraceBufferSizeInBytes)
#define OUTPUT_FILESIZE_MAX_LENGTH (100)

static uint32_t buffer_size;
static uint16_t num_threads;
static uint16_t num_configs;
static uint32_t *oldest_indices;
static uint8_t num_cache_levels;
static Configuration *configs;
static uint8_t **buffers;

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

static FILE *read_in_file_header (const char *filename) {
    FILE *f = fopen(filename, "rb");
    assert(f);

    // uint32_t, buffer size in bytes
    assert(fread(&buffer_size, sizeof(uint32_t), 1, f));
    assert(buffer_size);

    // uint16_t, number of configs
    assert(fread(&num_configs, sizeof(uint16_t), 1, f));
    assert(num_configs);

    // uint8_t num_cache_levels
    assert(fread(&num_cache_levels, sizeof(uint8_t), 1, f) == 1);

    configs = new Configuration[num_cache_levels];
    assert(num_cache_levels);

    num_threads = num_configs > MAX_NUM_THREADS ? MAX_NUM_THREADS : num_configs;

    // buffers
    buffers = new uint8_t*[num_threads];
    assert(buffers);
    for (uint64_t thread_id = 0; thread_id < num_threads; thread_id++) {
        buffers[thread_id] =  new uint8_t[buffer_size];
        assert(buffers[thread_id]);
    }
    return f;
}

int main (int argc, char* argv[]) {
    if (argc < 3) {
        printf("Please provide a .bin file to decode and an output filename\n");
        exit(1);
    }

    FILE* f_in = read_in_file_header(argv[1]);

    // Decode & write trace files
    FILE *f_out = nullptr;
    char output_filename_buffer[OUTPUT_FILESIZE_MAX_LENGTH];
    int output_filename_length = sprintf(output_filename_buffer, "%s", argv[2]);
    assert(output_filename_length > 0);
    uint16_t config_index = 0;
    for (uint16_t thread_id = 0; thread_id < num_threads && config_index < num_configs; thread_id++, config_index++) {

        uint32_t buffer_append_point_offset;
        assert(fread(&buffer_append_point_offset, sizeof(uint32_t), 1, f_in) == 1);
        assert(fread(&configs[0], sizeof(Configuration), num_cache_levels, f_in) == num_cache_levels);
        assert(fread(buffers[thread_id], sizeof(uint8_t), buffer_size, f_in) == buffer_size);

        uint8_t *last_entry_ptr = buffers[thread_id] + SIM_TRACE_LAST_ENTRY_OFFSET;

        char *output_filename_begin = output_filename_buffer + output_filename_length;
        for (uint8_t j = 0; j < num_cache_levels; j++) {
            int bytes_written = sprintf(output_filename_begin, "_%lu_%lu_%lu", configs[j].cacheSize, configs[j].blockSize, configs[j].associativity);
            output_filename_begin += bytes_written;
        }
        sprintf(output_filename_begin, ".txt");
        f_out = fopen(output_filename_buffer, "w");
        assert(f_out);
        fprintf(f_out, "Cycle\t\tCache level\tMessage\n");
        fprintf(f_out, "=============================================================\n");

        uint8_t *buffer_ptr = buffers[thread_id] + buffer_append_point_offset;
        bool wrapped = false;
        // Find sync pattern
        uint16_t bytes_lost = 0;
        for (; *((sync_pattern_t*)buffer_ptr) != sync_pattern; buffer_ptr++, bytes_lost++) {
            if (buffer_ptr >= last_entry_ptr) {
                buffer_ptr = buffers[thread_id];
                wrapped = true;
            }
        }
        uint64_t entry_counter = 0;
        uint64_t cycle = 0;
        fprintf(f_out, "%u bytes lost before first sync pattern found\n", bytes_lost);
        buffer_ptr += sizeof(sync_pattern_t);
        while (true) {
            if (wrapped && buffer_ptr >= buffers[thread_id] + buffer_append_point_offset) {
                break;
            }

            // Skip sync pattern
            if (entry_counter == SIM_TRACE_SYNC_INTERVAL) {
                assert(*((sync_pattern_t*) buffer_ptr) == sync_pattern);
                buffer_ptr += sizeof(sync_pattern_t);
                entry_counter = 0;
            }
            entry_counter++;

            SimTraceEntry entry = *((SimTraceEntry*) buffer_ptr);
            assert(entry.trace_entry_id < NUM_SIM_TRACE_ENTRIES);
            buffer_ptr += sizeof(SimTraceEntry);
            assert(buffer_ptr < buffers[thread_id] + buffer_size);
            cycle += entry.cycle_offset;
            fprintf(f_out, "%012lu\t%u\t\t", cycle, entry.cache_level);

            int num_args = sim_trace_entry_num_arguments[entry.trace_entry_id];
            switch (num_args)
            {
            case 0:
                fprintf(f_out, "%s", sim_trace_entry_definitions[entry.trace_entry_id]);
                break;
            case 1:
                fprintf(f_out, sim_trace_entry_definitions[entry.trace_entry_id], *((sim_trace_entry_data_t*)buffer_ptr));
                break;
            case 2:
                fprintf(f_out, sim_trace_entry_definitions[entry.trace_entry_id],
                    *((sim_trace_entry_data_t*)buffer_ptr), *((sim_trace_entry_data_t*)buffer_ptr + 1));
                break;
            case 3:
                fprintf(f_out, sim_trace_entry_definitions[entry.trace_entry_id],
                    *((sim_trace_entry_data_t*)buffer_ptr), *((sim_trace_entry_data_t*)buffer_ptr + 1), *((sim_trace_entry_data_t*)buffer_ptr + 2));
                break;
            case 4:
                fprintf(f_out, sim_trace_entry_definitions[entry.trace_entry_id],
                    *((sim_trace_entry_data_t*)buffer_ptr), *((sim_trace_entry_data_t*)buffer_ptr + 1),
                    *((sim_trace_entry_data_t*)buffer_ptr + 2), *((sim_trace_entry_data_t*)buffer_ptr + 3));
                break;
            case 5:
                fprintf(f_out, sim_trace_entry_definitions[entry.trace_entry_id],
                    *((sim_trace_entry_data_t*)buffer_ptr), *((sim_trace_entry_data_t*)buffer_ptr + 1),
                    *((sim_trace_entry_data_t*)buffer_ptr + 2), *((sim_trace_entry_data_t*)buffer_ptr + 3), *((sim_trace_entry_data_t*)buffer_ptr + 4));
                break;
            default:
                fprintf(stderr, "MAX_NUM_SIM_TRACE_VALUES is too low\n");
                break;
            }
            buffer_ptr += sizeof(sim_trace_entry_data_t) * num_args;
            if (buffer_ptr >= last_entry_ptr) {
                buffer_ptr = buffers[thread_id];
                wrapped = true;
            }
        }
        fclose(f_out);
    }
    delete[] oldest_indices;
    for (uint32_t thread_id = 0; thread_id; thread_id++) {
        delete[] buffers[thread_id];
    }
    delete[] configs;
    delete[] buffers;
    return 0;
}
