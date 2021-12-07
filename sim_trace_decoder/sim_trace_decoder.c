#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#ifndef SIM_TRACE
#define SIM_TRACE
#endif

#include "sim_trace.h"
#include "sim_trace_decoder.h"
#include "cache.h"

static uint32_t num_entries;
static uint16_t num_threads;
static uint32_t *oldest_indices;
static uint8_t num_cache_levels;
static config_t **configs;
static sim_trace_entry_t **buffers;

/**
 * File format is as follows:
 * uint32_t number of entries in each sim_trace
 * uint16_t number of threads
 * uint32_t array of indices to oldest entry
 * configs of each cache
 * buffers
 */

static void read_in_file (const char *filename) {
    FILE *f = fopen(filename, "rb");
    assert(f);

    // uint32_t, number of entries in each sim_trace
    fread(&num_entries, sizeof(uint32_t), 1, f);
    assert(num_entries);

    // uint16_t, number of threads
    fread(&num_threads, sizeof(uint16_t), 1, f);
    assert(num_threads);

    // array of uint32_t indices to oldest entry
    oldest_indices = (uint32_t*) malloc(sizeof(uint32_t) * num_threads);
    assert(fread(oldest_indices, sizeof(uint32_t), num_threads, f) == num_threads);

    // uint8_t num_cache_levels
    assert(fread(&num_cache_levels, sizeof(uint8_t), 1, f) == 1);

    // configs of each cache
    configs = (config_t**) malloc(sizeof(config_t*) * num_threads);
    assert(configs);
    for (uint8_t i = 0; i < num_threads; i++) {
        configs[i] = (config_t*) malloc(sizeof(config_t) * num_cache_levels);
        assert(configs[i]);
        assert(fread(configs[i], sizeof(config_t), num_cache_levels, f) == num_cache_levels);
    }

    // buffers
    buffers = (sim_trace_entry_t**) malloc(sizeof(sim_trace_entry_t*) * num_threads);
    assert(buffers);
    for (uint64_t thread_id = 0; thread_id < num_threads; thread_id++) {
        buffers[thread_id] = (sim_trace_entry_t*) malloc(SIM_TRACE_BUFFER_SIZE_IN_BYTES);
        assert(buffers[thread_id]);
        assert(fread(buffers[thread_id], sizeof(sim_trace_entry_t), SIM_TRACE_BUFFER_SIZE_IN_ENTRIES, f) == SIM_TRACE_BUFFER_SIZE_IN_ENTRIES);
    }
}

int main (int argc, char* argv[]) {
    if (argc < 3) {
        printf("Please provide a .bin file to decode and an output filename\n");
        exit(1);
    }

    read_in_file(argv[1]);

    // Decode & write trace files
    FILE *f_out = NULL;
    char output_filename_buffer[100];
    int output_filename_length = sprintf(output_filename_buffer, "%s", argv[2]);
    assert(output_filename_length > 0);
    for (uint16_t thread_id = 0; thread_id < num_threads; thread_id++) {
        char *output_filename_begin = output_filename_buffer + output_filename_length;
        for (uint8_t j = 0; j < num_cache_levels; j++) {
            int bytes_written = sprintf(output_filename_begin, "_%lu_%lu_%lu_", configs[thread_id][j].cache_size, configs[thread_id][j].block_size, configs[thread_id][j].num_blocks_per_slot);
            output_filename_begin += bytes_written;
        }
        sprintf(output_filename_begin, ".txt");
        f_out = fopen(output_filename_buffer, "w");
        assert(f_out);
        for (uint32_t buffer_index = oldest_indices[thread_id]; buffer_index < num_entries; buffer_index++) {
            int num_args = sim_trace_entry_num_arguments[buffers[thread_id][buffer_index].trace_entry_id];
            switch (num_args)
            {
            case 1:
                fprintf(f_out, sim_trace_entry_definitions[buffers[thread_id][buffer_index].trace_entry_id], buffers[thread_id][buffer_index].values[0]);
                break;
            case 2:
                fprintf(f_out, sim_trace_entry_definitions[buffers[thread_id][buffer_index].trace_entry_id],
                    buffers[thread_id][buffer_index].values[0], buffers[thread_id][buffer_index].values[1]);
                break;
            case 3:
                fprintf(f_out, sim_trace_entry_definitions[buffers[thread_id][buffer_index].trace_entry_id],
                    buffers[thread_id][buffer_index].values[0], buffers[thread_id][buffer_index].values[1], buffers[thread_id][buffer_index].values[2]);
                break;
            case 4:
                fprintf(f_out, sim_trace_entry_definitions[buffers[thread_id][buffer_index].trace_entry_id],
                    buffers[thread_id][buffer_index].values[0], buffers[thread_id][buffer_index].values[1],
                    buffers[thread_id][buffer_index].values[2], buffers[thread_id][buffer_index].values[3]);
                break;            
            default:
                fprintf(stderr, "MAX_NUM_SIM_TRACE_VALUES is too low\n");
                break;
            }
        }
        fclose(f_out);
    }
    free(oldest_indices);
    for (uint32_t thread_id = 0; thread_id; thread_id++) {
        free(buffers[thread_id]);
        free(configs[thread_id]);
    }
    free(configs);
    free(buffers);
    return 0;
}
