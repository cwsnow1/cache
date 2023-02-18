#ifndef SIM_TRACE
#define SIM_TRACE
#endif
#ifdef SIM_TRACE

#define MAX_NUM_SIM_TRACE_VALUES            (4)
#define SIM_TRACE_BUFFER_SIZE_IN_BYTES      (16777216) // 16MiB, per thread
#define SIM_TRACE_BUFFER_SIZE_IN_ENTRIES    (SIM_TRACE_BUFFER_SIZE_IN_BYTES / sizeof(sim_trace_entry_t))
#define SIM_TRACE_WARNING_THRESHOLD         (128)
#define MEMORY_USAGE_LIMIT                  ((uint64_t)UINT32_MAX + 1ULL) // 4 GiB

#define SIM_TRACE_FILENAME "sim_trace.bin"

typedef enum trace_entry_id_e {
    SIM_TRACE__ACCESS_BEGIN,
    SIM_TRACE__MISS,
    SIM_TRACE__LRU_UPDATE,
    SIM_TRACE__EVICT,
    SIM_TRACE__REQUEST_ADDED,
    SIM_TRACE__REQUEST_FAILED,
    SIM_TRACE__EVICT_FAILED,

    // Add new entries above this
    NUM_SIM_TRACE_ENTRIES,
    SIM_TRACE__INVALID,
} trace_entry_id_t;




typedef struct sim_trace_entry_s {
    uint64_t cycle;
    uint64_t values[MAX_NUM_SIM_TRACE_VALUES];
    trace_entry_id_t trace_entry_id;
    uint8_t cache_level;
} sim_trace_entry_t;

/**
 * @brief  Initializes the buffer and tracking info for sim traces
 * 
 * @param filename  The name of the file to be written
 * 
 * @return          File pointer to sim trace file opened
 */
FILE * sim_trace__init(const char *filename);

/**
 * @brief                   "Prints" a trace entry -- really adds it to a buffer to be written later
 * 
 * @param trace_entry_id    See sim_trace.h & sim_trace_decoder.h for entry info
 * @param cache             Pointer to cache structure making entry, used to print context info
 * @param values            Array of length MAX_NUM_SIM_TRACE_VALUES of values to be printed. Not all values need to be initialized if not necessary
 */
void sim_trace__print(trace_entry_id_t trace_entry_id, cache_t *cache, uint64_t *values);

/**
 * @brief       Writes the sim trace buffer for a thread after it completes (NOT THREAD-SAFE!)
 * 
 * @param cache Pointer to top level cache structure
 * @param f     File pointer to write to
 */
void sim_trace__write_thread_buffer(cache_t *cache, FILE *f);

/**
 * @brief   Closes file and frees memory used for sim trace
 * 
 * @param f Output file to close
 */
void sim_trace__reset(FILE *f);

#endif // SIM_TRACE
