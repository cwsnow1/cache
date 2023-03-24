#pragma once
#include "SimTracer.h"

const sync_pattern_t sync_pattern = 0xFFFFFFFF;

// Update both these tables when adding a new entry
const char* sim_trace_entry_definitions[NUM_SIM_TRACE_ENTRIES] = {
    "HIT:           pool_index=%02u, block_address=0x%04x%08lx, set_index=0x%08lx\n",
    "MISS:          pool_index=%02u, requesting block in set_index=0x%08lx\n",
    "LRU_UPDATE:    set_index=0x%08x, MRU: block_index=0x%02x, LRU: block_index=0x%02x\n",
    "EVICT:         set_index=0x%08x, block_index=0x%02x\n",
    "REQUEST_ADDED: pool_index=%02u, %c, address=0x%04x%08x, access_time=%u\n",
    "REQUEST_FAILED\n",
    "EVICT_FAILED\n",
};

const int sim_trace_entry_num_arguments[NUM_SIM_TRACE_ENTRIES] = {
    4,
    2,
    3,
    2,
    5,
    0,
    0,
};
