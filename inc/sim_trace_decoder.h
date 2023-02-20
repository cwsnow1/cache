#pragma once
#include "sim_trace.h"

const sync_pattern_t sync_pattern = 0xFFFFFFFF;

// Update both these tables when adding a new entry
const char* sim_trace_entry_definitions[NUM_SIM_TRACE_ENTRIES] = {
    "ACCESS_BEGIN:  pool_index=%02u, %c, block_address=0x%012lx, set_index=0x%08lx\n",
    "MISS:          pool_index=%02u, requesting block in set_index=0x%08lx\n",
    "LRU_UPDATE:    set_index=0x%08lx, MRU: block_index=0x%02x, LRU: block_index=0x%02x\n",
    "EVICT:         set_index=0x%08lx, block_index=0x%02x\n",
    "REQUEST_ADDED: pool_index=%02u, addr=0x%012lx, access_time=%u\n",
    "REQUEST_FAILED\n",
    "EVICT_FAILED\n",
};

const int sim_trace_entry_num_arguments[NUM_SIM_TRACE_ENTRIES] = {
    4,
    2,
    3,
    2,
    3,
    0,
    0,
};
