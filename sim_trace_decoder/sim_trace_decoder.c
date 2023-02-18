#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#ifndef SIM_TRACE
#define SIM_TRACE
#endif

#include "cache.h"
#include "sim_trace.h"
#include "sim_trace_decoder.h"

#define MAX_NUM_THREADS (MEMORY_USAGE_LIMIT / (uint64_t)SIM_TRACE_BUFFER_SIZE_IN_BYTES)

static uint32_t num_entries;
static uint16_t num_threads;
static uint16_t num_configs;
static uint32_t *oldest_indices;
static uint8_t num_cache_levels;
static config_t *configs;
static sim_trace_entry_t **buffers;

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

static FILE *read_in_file_header (const char *filename) {
    FILE *f = fopen(filename, "rb");
    assert(f);

    // uint32_t, number of entries in each sim_trace
    fread(&num_entries, sizeof(uint32_t), 1, f);
    assert(num_entries);

    // uint16_t, number of configs
    fread(&num_configs, sizeof(uint16_t), 1, f);
    assert(num_configs);

    // uint8_t num_cache_levels
    assert(fread(&num_cache_levels, sizeof(uint8_t), 1, f) == 1);

    configs = (config_t*) malloc(sizeof(config_t) * num_cache_levels);
    assert(num_cache_levels);

    num_threads = num_configs > MAX_NUM_THREADS ? MAX_NUM_THREADS : num_configs;

    // buffers
    buffers = (sim_trace_entry_t**) malloc(sizeof(sim_trace_entry_t*) * num_threads);
    assert(buffers);
    for (uint64_t thread_id = 0; thread_id < num_threads; thread_id++) {
        buffers[thread_id] = (sim_trace_entry_t*) malloc(SIM_TRACE_BUFFER_SIZE_IN_BYTES);
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
    FILE *f_out = NULL;
    char output_filename_buffer[100];
    int output_filename_length = sprintf(output_filename_buffer, "%s", argv[2]);
    assert(output_filename_length > 0);
    uint16_t config_index = 0;
    for (uint16_t thread_id = 0; thread_id < num_threads && config_index < num_configs; thread_id++, config_index++) {


        // array of uint32_t indices to oldest entry
        uint32_t oldest_index;
        assert(fread(&oldest_index, sizeof(uint32_t), 1, f_in) == 1);
        uint8_t num_cache_levels_local = num_cache_levels;

        size_t ret = fread(&configs[0], sizeof(config_t), num_cache_levels_local, f_in);

        assert(ret == num_cache_levels);

        assert(fread(buffers[thread_id], sizeof(sim_trace_entry_t), SIM_TRACE_BUFFER_SIZE_IN_ENTRIES, f_in) == SIM_TRACE_BUFFER_SIZE_IN_ENTRIES);
        char *output_filename_begin = output_filename_buffer + output_filename_length;
        for (uint8_t j = 0; j < num_cache_levels; j++) {
            int bytes_written = sprintf(output_filename_begin, "_%lu_%lu_%lu", configs[j].cache_size, configs[j].block_size, configs[j].associativity);
            output_filename_begin += bytes_written;
        }
        sprintf(output_filename_begin, ".txt");
        f_out = fopen(output_filename_buffer, "w");
        assert(f_out);
        fprintf(f_out, "Cycle\t\tCache level\tMessage\n");
        fprintf(f_out, "=============================================================\n");
        uint32_t buffer_index = oldest_index;
        uint64_t local_cycle = 0;
        for (uint32_t i = 0; i < num_entries; i++) {
            sim_trace_entry_t buffer = buffers[thread_id][buffer_index];
            fprintf(f_out, "%012lu\t%u\t\t", buffer.cycle, buffer.cache_level);
            assert(local_cycle <= buffer.cycle);
            local_cycle = buffer.cycle;
            int num_args = sim_trace_entry_num_arguments[buffer.trace_entry_id];
            switch (num_args)
            {
            case 0:
                fprintf(f_out, "%s", sim_trace_entry_definitions[buffer.trace_entry_id]);
                break;
            case 1:
                fprintf(f_out, sim_trace_entry_definitions[buffer.trace_entry_id], buffer.values[0]);
                break;
            case 2:
                fprintf(f_out, sim_trace_entry_definitions[buffer.trace_entry_id],
                    buffer.values[0], buffer.values[1]);
                break;
            case 3:
                fprintf(f_out, sim_trace_entry_definitions[buffer.trace_entry_id],
                    buffer.values[0], buffer.values[1], buffer.values[2]);
                break;
            case 4:
                fprintf(f_out, sim_trace_entry_definitions[buffer.trace_entry_id],
                    buffer.values[0], buffer.values[1],
                    buffer.values[2], buffer.values[3]);
                break;
            default:
                fprintf(stderr, "MAX_NUM_SIM_TRACE_VALUES is too low\n");
                break;
            }
            if (++buffer_index == num_entries) {
                buffer_index = 0;
            }
        }
        fclose(f_out);
    }
    free(oldest_indices);
    for (uint32_t thread_id = 0; thread_id; thread_id++) {
        free(buffers[thread_id]);
    }
    free(configs);
    free(buffers);
    return 0;
}
