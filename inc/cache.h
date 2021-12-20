#include "list.h"

#define ADDR_BITS                   (48)
#define MAX_TRACE_FILENAME_LENGTH   (100)

#define MAX_NUM_REQUESTS            (8)
#define CONSOLE_PRINT

struct RequestManager;

enum CacheLevels {
    L1,
    L2,
    L3,
    MAIN_MEMORY
};

enum Access {
    READ,
    WRITE
};

struct Instruction {
    uint64_t ptr;
    Access rw;
};

struct Request {
    Instruction instruction;
    uint64_t cycle;
    bool first_attempt;
};

struct Block {
    // The LSB of the block_addr will be the
    // set_index, and would be redudant to store
    uint64_t block_addr;
    bool dirty;
    bool valid;
};

struct Set {
    Block *ways;
    uint8_t *lru_list;
    bool busy;
};

struct Stats {
    uint64_t write_hits;
    uint64_t read_hits;
    uint64_t write_misses;
    uint64_t read_misses;
    uint64_t writebacks;
};

struct Config {
    uint64_t cache_size;
    uint64_t block_size;
    uint64_t associativity;
    uint8_t num_cache_levels;
};

class Cache {
public:
    Cache();

    /**
     * @brief                       Tries to initialize a cache structure
     * 
     * @param cache_level           Level of cache, 0 is L1, 1 is L2, etc.
     * @param configs               Array of structure containing all the config info needed per cache level
     * @param thread_id             Used to index into this thread's global data structures
     * @return true                 if cache config is valid
     */
    Cache(Cache *upper_cache, uint8_t cache_level, Config *configs, uint64_t thread_id);

    ~Cache();

    /**
     * @brief               Prints all relevant cache parameters to stdout
     */
    void printInfo();

    /**
     * @brief Simulates a read or write to an address
     * 
     * @param access    Instruction struct, comprises an address and access type (R/W)
     * @param cycle     Current clock cycle
     * 
     * @return          The index of the added request, -1 if request add failed
     */
    int16_t addAccessRequest(Instruction access, uint64_t cycle);

    /**
     * @brief                       Checks whether a given cache config is valid, i.e. not redundant
     * 
     * @param config                Structure containing all the config info needed
     * @return true                 if cache config is valid
     */
    bool isCacheConfigValid(Config config);

    /**
     * @brief                       Simulate a clock cycle in the cache structure(s)
     * 
     * @param cycle                 Current clock cycle
     * @param completed_requests    Out. An array of the request indices that were completed this tick. Length is the return value
     * 
     * @return                      Number of requests completed this tick
     */
    uint64_t processCache (uint64_t cycle, int16_t *completed_requests);

    uint8_t getCacheLevel() {
        return cache_level;
    }

protected:
    /**
     *  @brief Translates raw address to block address
     * 
     *  @param cache        Cache struct to use for config
     *  @param addr         Raw address, 64 bits
     *  @return             Block address, i.e. x MSB of the raw address
     */
    uint64_t addrToBlockAddr (uint64_t addr);

    /**
     *  @brief Translates a block address to a set index
     * 
     *  @param cache        Cache struct to use for config
     *  @param block_addr   Block address, i.e. the raw address shifted
     *  @return             Set index
     */
    uint64_t blockAddrToSetIndex (uint64_t block_addr);

    /**
     *  @brief Translates a raw address to a set index
     * 
     *  @param block_addr   Raw address, 64 bits
     *  @return             Set index
     */
    uint64_t addrToSetIndex (uint64_t addr);

    /**
     * @brief               Reorder the LRU list for the given set
     * 
     * @param set_index    Set whose LRU list is to be reordered
     * @param mru_index     Block index that is now the most recently used
     */
    void updateLRUList (uint64_t set_index, uint8_t mru_index);

    /**
     * @brief               Handles the eviction and subsequent interactions with lower cache(s)
     * 
     * @param set_index     Set index from which block needs to be evicted
     * @param block_addr    Block address that will replace the evicted block
     * @param cycle         Current clock cycle
     * @return              Block index within the provided set that the new block occupies, -1 if evict request failed
     */
    int16_t evictBlock (uint64_t set_index, uint64_t block_addr, uint64_t cycle);

    /**
     * @brief               Searches the given set for the given block address
     * 
     * @param set_index     Set index to search
     * @param block_addr    Block address for which to search
     * @param block_index   Output: The block index within the set iff found
     * @return true         if the block is found in the set
     */
    bool findBlockInSet (uint64_t set_index, uint64_t block_addr, uint8_t *block_index);

    /**
     * @brief               Acquires the given block address into the given set. Makes any subsequent calls necessary to lower caches
     * 
     * @param set_index     Set in which to put new block
     * @param block_addr    Block address of block to acquire
     * @param cycle         Current clock cycle
     * @return              Block index acquired within set, -1 if request failed
     */
    int16_t requestBlock (uint64_t set_index, uint64_t block_addr, uint64_t cycle);

    /**
     * @brief           Attempts a read or write to the given cache
     * 
     * @param request   Request structure to attempt
     * @param cycle     Current clock cycle
     * @return true     If the request was completed and need not be called again
     */
    bool handleAccess (Request request, uint64_t cycle);

    // Multi-threading fields
    uint64_t thread_id;

    // Cache hierarchy fields
    Cache *upper_cache;
    Cache *lower_cache;
    uint8_t cache_level;

    // Cache sizing fields
    Config config;
    uint64_t num_sets;

    // Sizing fields used in calculations
    uint64_t block_size_bits;
    uint64_t block_addr_to_set_index_mask;

    // Data
    Set *sets;
    RequestManager *request_manager;

    Stats stats;
};

class MainMemory: public Cache {
public:
    MainMemory(Cache *upper_cache, uint64_t thread_id);
    ~MainMemory();
};

struct TestParams {
    uint8_t  num_cache_levels;
    uint64_t min_block_size;
    uint64_t max_block_size;
    uint64_t min_cache_size;
    uint64_t max_cache_size;
    uint8_t  min_blocks_per_set;
    uint8_t  max_blocks_per_set;
    int32_t  max_num_threads;
};
