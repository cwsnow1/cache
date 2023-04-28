#pragma once
#include <stdio.h>
#include <stdint.h>

#include "Cache.h"

#ifndef SIM_TRACE
#define SIM_TRACE (1)
#endif

typedef enum trace_entry_id_e {
    SIM_TRACE__HIT,
    SIM_TRACE__MISS,
    SIM_TRACE__LRU_UPDATE,
    SIM_TRACE__EVICT,
    SIM_TRACE__REQUEST_ADDED,
    SIM_TRACE__REQUEST_FAILED,
    SIM_TRACE__EVICT_FAILED,

    // Add new entries above this
    NUM_SIM_TRACE_ENTRIES,
    SIM_TRACE__INVALID,
} __attribute__ ((packed)) trace_entry_id_t;

typedef uint32_t sim_trace_entry_data_t;
typedef uint32_t sync_pattern_t;
struct SimTraceEntry {
    uint16_t cycle_offset;
    trace_entry_id_t trace_entry_id;
    CacheLevel cache_level;

    SimTraceEntry(uint16_t cycle_offset, trace_entry_id_t trace_entry_id, CacheLevel cache_level) {
        this->cycle_offset = cycle_offset;
        this->trace_entry_id = trace_entry_id;
        this->cache_level = cache_level;
    };

} __attribute__ ((packed));


#define MAX_NUM_SIM_TRACE_VALUES            (5)
constexpr uint64_t kSimTraceBufferSizeInBytes = 16777216UL; // 16MiB, per thread
#define SIM_TRACE_SYNC_INTERVAL             (1 << 15) // Number of entries between syncs, meaning up to this many entries could be lost at decode
#define SIM_TRACE_LAST_ENTRY_OFFSET         (kSimTraceBufferSizeInBytes - (MAX_NUM_SIM_TRACE_VALUES * sizeof(sim_trace_entry_data_t)) - sizeof(SimTraceEntry) - sizeof(sync_pattern_t))
#define MEMORY_USAGE_LIMIT                  ((uint64_t)UINT32_MAX + 1ULL) // 4 GiB
#define SIM_TRACE_WARNING_THRESHOLD         ((MEMORY_USAGE_LIMIT >> 1) / kSimTraceBufferSizeInBytes)

#define SIM_TRACE_FILENAME "sim_trace.bin"

#if (SIM_TRACE == 1)
class SimTracer {
public:
    SimTracer();

    /**
     * @brief Construct a new Sim Tracer object
     * 
     * @param filename name of file to open
     * @param num_configs total number of configs under test
     */
    SimTracer(const char *filename, uint64_t num_configs);

    /**
     * @brief Destroy the Sim Tracer object
     * 
     */
    ~SimTracer();

    /**
     * @brief                   "Prints" a trace entry -- really adds it to a buffer to be written later
     * 
     * @param trace_entry_id    See sim_trace.h & sim_trace_decoder.h for entry info
     * @param memory            Pointer to memory object making entry, used to print context info
     * @param values            Up to MAX_NUM_SIM_TRACE_VALUES values the size of sim_trace_entry_data_t
     */
    void Print(trace_entry_id_t trace_entry_id, Memory *memory, ...);

    /**
     * @brief                   "Prints" a trace entry -- really adds it to a buffer to be written later
     * 
     * @param trace_entry_id    See sim_trace.h & sim_trace_decoder.h for entry info
     * @param memory            Pointer to memory object making entry, used to print context info
     * @param values            Up to MAX_NUM_SIM_TRACE_VALUES values the size of sim_trace_entry_data_t
     */
    void Print(trace_entry_id_t trace_entry_id, Cache *cache, ...);

    /**
     * @brief       Writes the sim trace buffer for a thread after it completes (NOT THREAD-SAFE!)
     * 
     * @param cache Pointer to top level cache structure
     */
    void WriteThreadBuffer(Cache *cache);

private:

    FILE *f_;
    uint8_t **buffer_append_points_;
    uint8_t **sim_trace_buffer_;
    uint64_t *entry_counters_;
    uint64_t *previous_cycle_counter_;
};



#else

class SimTracer {
public:
    SimTracer() = default;
    void Print(...) {};
};

#endif // SIM_TRACE
