#ifndef SIM_TRACE
#define SIM_TRACE
#endif
#ifdef SIM_TRACE

#define MAX_NUM_SIM_TRACE_VALUES            (4)
#define SIM_TRACE_BUFFER_SIZE_IN_BYTES      (1048576U) // 1MiB, per thread
#define SIM_TRACE_BUFFER_SIZE_IN_ENTRIES    (SIM_TRACE_BUFFER_SIZE_IN_BYTES / sizeof(sim_trace_entry_t))
#define SIM_TRACE_WARNING_THRESHOLD         (128)

#define SIM_TRACE_FILENAME "sim_trace.bin"

typedef enum trace_entry_id_e {
    SIM_TRACE__ACCESS_BEGIN,
    SIM_TRACE__MISS,
    SIM_TRACE__LRU_UPDATE,
    SIM_TRACE__EVICT,

    // Add new entries above this
    NUM_SIM_TRACE_ENTRIES,
    SIM_TRACE__INVALID,
} trace_entry_id_t;


typedef struct sim_trace_entry_s {
    trace_entry_id_t trace_entry_id;
    uint64_t values[MAX_NUM_SIM_TRACE_VALUES];
} sim_trace_entry_t;

/**
 * @brief  Initializes the buffer and tracking info for sim traces
 * 
 * @return 0 if successful, -1 if the user cancels
 */
int sim_trace__init(void);

/**
 * @brief                   "Prints" a trace entry -- really adds it to a buffer to be written later
 * 
 * @param trace_entry_id    See sim_trace.h & sim_trace_decoder.h for entry info
 * @param thread_id         Thread ID of the calling thread to keep buffers separate
 * @param values            Array of length MAX_NUM_SIM_TRACE_VALUES of values to be printed. Not all values need to be initialized if not necessary
 */
void sim_trace__print(trace_entry_id_t trace_entry_id, uint64_t thread_id, uint64_t *values);

/**
 * @brief           Write all the sim trace buffers to a binary file and free all strcutures
 * 
 * @param filename  The name of the file to be written
 */
void sim_trace__write_to_file_and_exit(const char *filename);

#endif // SIM_TRACE
