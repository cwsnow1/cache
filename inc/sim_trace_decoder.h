
// Update both these tables when adding a new entry
const char* sim_trace_entry_definitions[NUM_SIM_TRACE_ENTRIES] = {
    "ACCESS_BEGIN: %c, cache_level=%hhu, block_address=0x%012lx, slot_index=0x%08lx",
    "MISS: Requesting block in slot_index=0x%08lx",
    "LRU_UPDATE: slot_index=0x%08lx, MRU: block_index=0x%02x, LRU: block_index=0x%02x",
    "EVICT: slot_index=0x%08lx, block_index=0x%02x",
};

const int sim_trace_entry_num_arguments[NUM_SIM_TRACE_ENTRIES] = {
    4,
    1,
    3,
    2,
};
