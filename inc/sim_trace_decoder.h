#pragma once
#include <inttypes.h>
#include "SimTracer.h"

constexpr sync_pattern_t kSyncPattern = 0xFFFFFFFF;

// Update both these tables when adding a new entry
const char* simTraceEntryDefinitions[kNumberOfSimTraceEntries] = {
    "HIT:           pool_index=%02u, blockAddress=0x%04x%08" PRIx64 ", set_index=0x%08" PRIx64 "\n",
    "MISS:          pool_index=%02u, requesting block in set_index=0x%08" PRIx64 "\n",
    "LRU_UPDATE:    set_index=0x%08x, MRU: block_index=0x%02x, LRU: block_index=0x%02x\n",
    "EVICT:         set_index=0x%08x, block_index=0x%02x\n",
    "REQUEST_ADDED: pool_index=%02u, access_type=%u, address=0x%04x%08x, access_time=%u\n",
    "REQUEST_FAILED\n",
    "EVICT_FAILED\n",
};

constexpr int kNumberOfArgumentsInSimTraceEntry[kNumberOfSimTraceEntries] = {
    4,
    2,
    3,
    2,
    5,
    0,
    0,
};
