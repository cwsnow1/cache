#pragma once
#include <stdio.h>
#include <stdint.h>

#include "Cache.h"

#ifndef SIM_TRACE
#define SIM_TRACE (1)
#endif

PACK(enum TraceEntryId {
    SIM_TRACE__HIT              COMMA
    SIM_TRACE__MISS             COMMA
    SIM_TRACE__LRU_UPDATE       COMMA
    SIM_TRACE__EVICT            COMMA
    SIM_TRACE__REQUEST_ADDED    COMMA
    SIM_TRACE__REQUEST_FAILED   COMMA
    SIM_TRACE__EVICT_FAILED     COMMA

    // Add new entries above this
    kNumberOfSimTraceEntries    COMMA
    SIM_TRACE__INVALID          COMMA
});

typedef uint32_t sim_trace_entry_data_t;
typedef uint32_t sync_pattern_t;
PACK(struct SimTraceEntry {
    uint16_t cycle_offset;
    TraceEntryId trace_entry_id;
    CacheLevel cache_level;

    SimTraceEntry(uint16_t cycle_offset, TraceEntryId trace_entry_id, CacheLevel cache_level) {
        this->cycle_offset = cycle_offset;
        this->trace_entry_id = trace_entry_id;
        this->cache_level = cache_level;
    };

});


#define MAX_NUM_SIM_TRACE_VALUES            (5)
constexpr uint64_t kSimTraceBufferSizeInBytes = 16777216UL; // 16MiB, per thread
#define SIM_TRACE_SYNC_INTERVAL             (1 << 15) // Number of entries between syncs, meaning up to this many entries could be lost at decode
#define SIM_TRACE_LAST_ENTRY_OFFSET         (kSimTraceBufferSizeInBytes - (MAX_NUM_SIM_TRACE_VALUES * sizeof(sim_trace_entry_data_t)) - sizeof(SimTraceEntry) - sizeof(sync_pattern_t))
constexpr uint64_t MEMORY_USAGE_LIMIT     = ((uint64_t)UINT32_MAX + 1ULL); // 4 GiB
#define SIM_TRACE_WARNING_THRESHOLD         ((MEMORY_USAGE_LIMIT >> 1) / kSimTraceBufferSizeInBytes)

#define SIM_TRACE_FILENAME "sim_trace.bin"

#if (SIM_TRACE == 1)
class SimTracer {
public:
    SimTracer();

    /**
     * @brief                   Construct a new Sim Tracer object
     * 
     * @param filename          name of file to open
     * @param numberOfConfigs   total number of configs under test
     */
    SimTracer(const char *filename, uint64_t numberOfConfigs);

    /**
     * @brief Destroy the Sim Tracer object
     * 
     */
    ~SimTracer();

    /**
     * @brief                   "Prints" a trace entry -- really adds it to a buffer to be written later
     * 
     * @param traceEntryId      See sim_trace.h & sim_trace_decoder.h for entry info
     * @param pMemory           Pointer to memory object making entry, used to print context info
     * @param ...               Up to MAX_NUM_SIM_TRACE_VALUES values the size of sim_trace_entry_data_t
     */
    void Print(TraceEntryId traceEntryId, Memory *pMemory, ...);

    /**
     * @brief           Writes the sim trace buffer for a thread after it completes (NOT THREAD-SAFE!)
     * 
     * @param pCache    Pointer to top level cache structure
     */
    void WriteThreadBuffer(Cache *pCache);

private:

    FILE *pFile_;
    uint8_t **pBufferAppendPoints_;
    uint8_t **pSimTraceBuffer_;
    uint64_t *pEntryCounters_;
    uint64_t *pPreviousCycleCounter_;
};



#else

class SimTracer {
public:
    SimTracer() = default;
    void Print(...) {};
};

#endif // SIM_TRACE
