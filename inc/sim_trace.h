#define SIM_TRACE (0)

#if (SIM_TRACE == 1)

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
} __attribute__ ((packed)) trace_entry_id_t;

typedef uint64_t sim_trace_entry_data_t;
typedef uint32_t sync_pattern_t;
typedef struct sim_trace_entry_s {
    uint64_t cycle;
    trace_entry_id_t trace_entry_id;
    cache_level_t cache_level;
} __attribute__ ((packed)) sim_trace_entry_t;


#define MAX_NUM_SIM_TRACE_VALUES            (4)
#define SIM_TRACE_BUFFER_SIZE_IN_BYTES      (16777216) // 16MiB, per thread
#define SIM_TRACE_SYNC_INTERVAL             (256) // Number of entries between syncs, meaning up to this many entries could be lost at decode
#define SIM_TRACE_LAST_ENTRY_OFFSET         (SIM_TRACE_BUFFER_SIZE_IN_BYTES - (MAX_NUM_SIM_TRACE_VALUES * sizeof(sim_trace_entry_data_t)) - sizeof(sim_trace_entry_t) - sizeof(sync_pattern_t))
#define SIM_TRACE_BUFFER_SIZE_IN_ENTRIES    (SIM_TRACE_BUFFER_SIZE_IN_BYTES / sizeof(sim_trace_entry_t))
#define MEMORY_USAGE_LIMIT                  ((uint64_t)UINT32_MAX + 1ULL) // 4 GiB
#define SIM_TRACE_WARNING_THRESHOLD         ((MEMORY_USAGE_LIMIT >> 1) / SIM_TRACE_BUFFER_SIZE_IN_BYTES)

#define SIM_TRACE_FILENAME "sim_trace.bin"


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
void sim_trace__print(trace_entry_id_t trace_entry_id, cache_t *cache, ...);

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

#else

#define sim_trace__print(...)

#endif // SIM_TRACE
