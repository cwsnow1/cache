#define SIM_TRACE
#ifdef SIM_TRACE

#define MAX_NUM_SIM_TRACE_VALUES            (4)
#define SIM_TRACE_BUFFER_SIZE_IN_BYTES      (1048576U) // 1MiB, per thread
#define SIM_TRACE_BUFFER_SIZE_IN_ENTRIES    (SIM_TRACE_BUFFER_SIZE_IN_BYTES / sizeof(sim_trace_entry_t))


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

void sim_trace__init(void);
void sim_trace__print(trace_entry_id_t trace_entry_id, uint64_t thread_id, uint64_t *values);
void sim_trace__write_to_file_and_exit(const char *filename);

#endif // SIM_TRACE
