#define ADDR_BITS                   (48)
#define MAX_TRACE_FILENAME_LENGTH   (100)

typedef enum access_e {
    READ,
    WRITE
} access_t;

typedef struct instruction_s {
    uint64_t ptr;
    access_t rw;
} instruction_t;

typedef struct block_s {
    // The LSB of the block_addr will be the
    // slot_index, and would be redudant to store
    uint64_t block_addr;
    bool dirty;
    bool valid;
} block_t;

typedef struct slot_s {
    block_t *blocks;
    uint8_t *lru_list;
} slot_t;

typedef struct stats_s {
    uint64_t write_hits;
    uint64_t read_hits;
    uint64_t write_misses;
    uint64_t read_misses;
    uint64_t writebacks;
} stats_t;

typedef struct cache_s {
    uint64_t thread_id;
    struct cache_s *lower_cache;
    uint8_t cache_level;
    uint64_t cache_size;
    uint64_t block_size;
    uint64_t block_size_bits;
    uint64_t num_blocks_per_slot;
    uint64_t num_slots;
    uint64_t block_addr_to_slot_index_mask;
    uint64_t num_blocks;
    slot_t  *slots;
    stats_t stats;
} cache_t;

typedef struct config_s {
    uint64_t cache_size;
    uint64_t block_size;
    uint64_t num_blocks_per_slot;
} config_t;

typedef struct test_params_s {
    uint8_t  num_cache_levels;
    uint64_t min_block_size;
    uint64_t max_block_size;
    uint64_t min_cache_size;
    uint64_t max_cache_size;
    uint8_t  min_blocks_per_slot;
    uint8_t  max_blocks_per_slot;
    int32_t  max_num_threads;
} test_params_t;

/**
 * @brief                       Checks whether a given cache config is valid, i.e. not redundant
 * 
 * @param config                Structure containing all the config info needed
 * @return true                 if cache config is valid
 */
bool cache__is_cache_config_valid(config_t config);

/**
 * @brief                       Tries to initialize a cache structure
 * 
 * @param caches                Array of cache structure pointers, length is number of cache levels
 * @param cache_level           Level of cache, 0 is L1, 1 is L2, etc.
 * @param configs               Array of structure containing all the config info needed per cache level
 * @return true                 if cache config is valid
 */
bool cache__init(cache_t *caches, uint8_t cache_level, config_t *configs, uint64_t thread_id);

/**
 * @brief               Resets all caches at and below the level provided. Frees memory, resets parameters, etc.
 * 
 * @param cache         Pointer to the top level cache, ideally
 */
void cache__reset(cache_t* cache);

/**
 * @brief               Prints all relevant cache parameters to stdout
 * 
 * @param me            Cache structure
 */
void cache__print_info(cache_t *me);

/**
 * @brief Simulates a read or write to an address
 * 
 * @param cache    The cache struct being written/read
 * @param access   Instruction struct, comprises an address and access type (R/W)
 */
void cache__handle_access(cache_t *cache, instruction_t access);
