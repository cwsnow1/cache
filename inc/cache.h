#include "list.h"

#define ADDR_BITS                   (48)
#define MAX_TRACE_FILENAME_LENGTH   (100)

#define MAX_NUM_REQUESTS            (8)
//#define CONSOLE_PRINT

enum cache_levels {
    L1,
    L2,
    L3,
    MAIN_MEMORY
};

typedef enum access_e {
    READ,
    WRITE
} access_t;

typedef struct instruction_s {
    uint64_t ptr;
    access_t rw;
} instruction_t;

typedef struct request_s {
    instruction_t instruction;
    uint64_t cycle;
    bool valid;
} request_t;

typedef struct request_manager_s {
    request_t *request_pool;
    double_list_t *outstanding_requests;
    double_list_t *free_requests;
    uint64_t max_outstanding_requests;
} request_manager_t;

typedef struct block_s {
    // The LSB of the block_addr will be the
    // set_index, and would be redudant to store
    uint64_t block_addr;
    bool dirty;
    bool valid;
} block_t;

typedef struct set_s {
    block_t *ways;
    uint8_t *lru_list;
    bool busy;
} set_t;

typedef struct stats_s {
    uint64_t write_hits;
    uint64_t read_hits;
    uint64_t write_misses;
    uint64_t read_misses;
    uint64_t writebacks;
} stats_t;

typedef struct cache_s {
    uint64_t thread_id;
    struct cache_s *upper_cache;
    struct cache_s *lower_cache;
    uint8_t cache_level;
    uint64_t cache_size;
    uint64_t block_size;
    uint64_t block_size_bits;
    uint64_t associativity;
    uint64_t num_sets;
    uint64_t block_addr_to_set_index_mask;
    uint64_t num_blocks;
    set_t  *sets;
    stats_t stats;
    request_manager_t request_manager;
} cache_t;

typedef struct config_s {
    uint64_t cache_size;
    uint64_t block_size;
    uint64_t associativity;
    uint8_t num_cache_levels;
} config_t;

typedef struct test_params_s {
    uint8_t  num_cache_levels;
    uint64_t min_block_size;
    uint64_t max_block_size;
    uint64_t min_cache_size;
    uint64_t max_cache_size;
    uint8_t  min_blocks_per_set;
    uint8_t  max_blocks_per_set;
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
bool cache__add_access_request(cache_t *cache, instruction_t access, uint64_t cycle);

void cache__process_cache (cache_t *cache, uint64_t cycle);
