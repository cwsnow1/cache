#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "sim_trace.h"
#ifdef SIM_TRACE

static uint32_t *thread_indices;
static sim_trace_entry_t **sim_trace_buffer;
extern uint64_t num_configs;

int sim_trace__init(void) {
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
            return -1;
        }
    }
    assert(num_configs);
    assert(SIM_TRACE_BUFFER_SIZE_IN_BYTES <= UINT32_MAX);
    thread_indices = (uint32_t*) malloc(sizeof(uint32_t) * num_configs);
    memset(thread_indices, 0, sizeof(uint32_t) * num_configs);
    sim_trace_buffer = (sim_trace_entry_t**) malloc(sizeof(sim_trace_entry_t*) * num_configs);
    for (uint64_t i = 0; i < num_configs; i++) {
        sim_trace_buffer[i] = (sim_trace_entry_t*) malloc(SIM_TRACE_BUFFER_SIZE_IN_BYTES);
        memset(sim_trace_buffer[i], SIM_TRACE__INVALID, SIM_TRACE_BUFFER_SIZE_IN_BYTES);
    }
    return 0;
}

void sim_trace__print(trace_entry_id_t trace_entry_id, uint64_t thread_id, uint64_t *values) {
    sim_trace_buffer[thread_id][thread_indices[thread_id]].trace_entry_id = trace_entry_id;
    memcpy(&sim_trace_buffer[thread_id][thread_indices[thread_id]].values[0], values, sizeof(uint64_t) * MAX_NUM_SIM_TRACE_VALUES);
    // Roll over when buffer is filled
    if (++thread_indices[thread_id] == SIM_TRACE_BUFFER_SIZE_IN_ENTRIES) {
        thread_indices[thread_id] = 0;
    }
}

/**
 * File format is as follows:
 * uint32_t number of entries in each sim_trace
 * uint16_t number of threads
 * uint32_t array of indices to oldest entry
 * buffers
 */


void sim_trace__write_to_file_and_exit(const char *filename) {
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
    // uint16_t number of threads
    uint16_t num_threads = (uint16_t) num_configs;
    ret = fwrite(&num_threads, sizeof(uint16_t), 1, f);
    assert(ret == 1);
    // uint32_t array of indices to oldest entry
    uint32_t *oldest_indices = (uint32_t*) malloc(sizeof(uint32_t) * num_threads);
    for (uint16_t i = 0; i < num_threads; i++) {
        oldest_indices[i] = thread_indices[i] + 1;
        if (oldest_indices[i] == SIM_TRACE_BUFFER_SIZE_IN_ENTRIES || (trace_entry_id_t) sim_trace_buffer[i][oldest_indices[i]].trace_entry_id == SIM_TRACE__INVALID) {
            oldest_indices[i] = 0;
        }
    }
    ret = fwrite(oldest_indices, sizeof(uint32_t), num_threads, f);
    assert(ret == num_threads);
    free(oldest_indices);
    free(thread_indices);
    // buffers
    for (uint16_t i = 0; i < num_threads; i++) {
        ret = fwrite(sim_trace_buffer[i], sizeof(sim_trace_entry_t), SIM_TRACE_BUFFER_SIZE_IN_ENTRIES, f);
        assert(ret == SIM_TRACE_BUFFER_SIZE_IN_ENTRIES);
        free(sim_trace_buffer[i]);
    }
    free(sim_trace_buffer);
    fclose(f);
}
#endif
