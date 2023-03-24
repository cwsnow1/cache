#pragma once
#include "list.h"

constexpr uint64_t kMaxNumberOfRequests = 8;

enum CacheLevel { kL1, kL2, kL3, kMainMemory } __attribute__((packed));

typedef enum access_e { READ, WRITE } access_t;

enum Status {
    kHit,
    kMiss,
    kMissing,
    kBusy,
};

struct Instruction {
    uint64_t ptr;
    access_t rw;

    Instruction() {}
    Instruction(uint64_t ptr, access_t rw) {
        this->ptr = ptr;
        this->rw = rw;
    }
};

struct Request {
    Instruction instruction;
    uint64_t cycle;
    uint64_t cycle_to_call_back;
    bool first_attempt;
};

struct RequestManager {
    Request *request_pool;
    DoubleList *waiting_requests;
    DoubleList *free_requests;
    DoubleList *busy_requests;
    uint64_t max_outstanding_requests;
};

struct Block {
    // The LSB of the block_address will be the
    // set_index, and would be redudant to store
    uint64_t block_address;
    bool dirty;
    bool valid;

    Block() {
        block_address = 0;
        dirty = false;
        valid = false;
    }
};

struct Set {
    Block *ways;
    uint8_t *lru_list;
    bool busy;

    Set() {
        ways = NULL;
        lru_list = NULL;
        busy = false;
    }
};

struct Statistics {
    uint64_t write_hits = 0;
    uint64_t read_hits = 0;
    uint64_t write_misses = 0;
    uint64_t read_misses = 0;
    uint64_t writebacks = 0;
};

struct Configuration {
    uint64_t cache_size;
    uint64_t block_size;
    uint64_t associativity;

    Configuration() = default;
    Configuration(uint64_t cache_size, uint64_t block_size,
                  uint64_t associativity) {
        this->cache_size = cache_size;
        this->block_size = block_size;
        this->associativity = associativity;
    }
};

class Cache {
   public:
    Cache() = default;
    /**
     * @brief                       Tries to initialize a cache structure
     *
     * @param cache_level           Level of cache
     * @param num_cache_levels      Number of cache levels
     * @param configs               Array of structure containing all the config
     * info needed per cache level
     * @param config_index          Index in global caches structure
     */
    Cache(Cache *upper_cache, CacheLevel cache_level, uint8_t num_cache_levels,
          Configuration *configs, uint64_t config_index);

    /**
     * @brief Destroy the Cache object
     *
     */
    ~Cache();

    /**
     * @brief                       Checks whether a given cache config is
     * valid, i.e. not redundant
     *
     * @param config                Structure containing all the config info
     * needed
     * @return true                 if cache config is valid
     */
    static bool IsCacheConfigValid(Configuration config);

    /**
     * @brief       Allocates memory needed for cache struture, recursively
     * calls lower caches
     *
     */
    void AllocateMemory();

    /**
     * @brief           Sets the thread index/ID of the cache instances
     *
     * @param thread_id Thread ID to be set
     */
    void SetThreadId(uint64_t thread_id);

    /**
     * @brief       Frees all memory allocated by cache structures, recursively
     * calls all lower caches
     */
    void FreeMemory();

    /**
     * @brief Simulates a read or write to an address
     *
     * @param access    Instruction struct, comprises an address and access type
     * (R/W)
     * @param cycle     Current clock cycle
     *
     * @return          The index of the added request, -1 if request add failed
     */
    int16_t AddAccessRequest(Instruction access, uint64_t cycle);

    /**
     * @brief                       Simulate a clock cycle in the cache
     * structure(s)
     *
     * @param cycle                 Current clock cycle
     * @param completed_requests    Out. An array of the request indices that
     * were completed this tick. Length is the return value
     *
     * @return                      Number of requests completed this tick
     */
    uint64_t ProcessCache(uint64_t cycle, int16_t *completed_requests);

    /**
     * @brief Get the Earliest Next Useful Cycle
     *
     * @return uint64_t the cycle wherein the next useful piece of work can be
     * done
     */
    uint64_t GetEarliestNextUsefulCycle();

    /**
     * @brief Get the cache level
     *
     * @return CacheLevel
     */
    inline CacheLevel GetCacheLevel();

    /**
     * @brief Get the Stats object
     *
     * @return Statistics
     */
    inline Statistics GetStats();

    /**
     * @brief Get the Config object
     *
     * @return Configuration
     */
    inline Configuration GetConfig();

    /**
     * @brief Get the cycle count
     *
     * @return uint64_t
     */
    inline uint64_t GetCycle();

    /**
     * @brief Get the Top Level Cache object
     *
     * @return Cache* top level cache object
     */
    Cache *GetTopLevelCache() {
        Cache *upperCache = upper_cache_;
        while (upperCache->upper_cache_ != NULL) {
            upperCache = upperCache->upper_cache_;
        }
        return upperCache;
    }

    /**
     * @brief Get the Lower Cache object
     *
     * @return Cache* cache object one level lower
     */
    Cache *GetLowerCache() { return lower_cache_; }

    /**
     * @brief Determines whether any useful work was done in the last cycle
     *
     * @return true if work was done
     * @return false id work wasn't done
     */
    bool WasWorkDoneThisCycle() { return work_done_this_cycle_; }

    // Multi-threading fields
    // TODO: move these out of class
    uint64_t thread_id_;
    uint64_t config_index_;

   private:
    /**
     * @brief Initializes the private, never-missing cache that represents main
     * memory
     *
     * @param lowest_cache The lowest level of cache that will be main memory's
     * upper_cache
     */
    void initMainMemory(Cache *lowest_cache);

    /**
     * @brief Initializes the request manager and the request lists it maintains
     *
     */
    void initRequestManager();

    /**
     *  @brief Translates raw address to block address
     *
     *  @param address         Raw address, 64 bits
     *  @return             Block address, i.e. x MSB of the raw address
     */
    inline uint64_t addressToBlockAddress(uint64_t address);

    /**
     *  @brief Translates a block address to a set index
     *
     *  @param block_address   Block address, i.e. the raw address shifted
     *  @return             Set index
     */
    inline uint64_t blockAddressToSetIndex(uint64_t block_address);

    /**
     *  @brief Translates a raw address to a set index
     *
     *  @param block_address   Raw address, 64 bits
     *  @return             Set index
     */
    inline uint64_t addressToSetIndex(uint64_t address);

    /**
     * @brief               Reorder the LRU list for the given set
     *
     * @param set_index     Set whose LRU list is to be reordered
     * @param mru_index     Block index that is now the most recently used
     */
    void updateLRUList(uint64_t set_index, uint8_t mru_index);

    /**
     * @brief               Handles the eviction and subsequent interactions
     * with lower cache(s)
     *
     * @param set_index     Set index from which block needs to be evicted
     * @param block_address    Block address that will replace the evicted block
     * @return              Block index within the provided set that the new
     * block occupies, -1 if evict request failed
     */
    int16_t evictBlock(uint64_t set_index, uint64_t block_address);

    /**
     * @brief               Searches the given set for the given block address
     *
     * @param set_index     Set index to search
     * @param block_address    Block address for which to search
     * @param block_index   Output: The block index within the set iff found
     * @return true         if the block is found in the set
     */
    bool findBlockInSet(uint64_t set_index, uint64_t block_address,
                        uint8_t *block_index);

    /**
     * @brief               Acquires the given block address into the given set.
     * Makes any subsequent calls necessary to lower caches
     *
     * @param set_index     Set in which to put new block
     * @param block_address    Block address of block to acquire
     * @return              Block index acquired within set, -1 if request
     * failed
     */
    int16_t requestBlock(uint64_t set_index, uint64_t block_address);

    /**
     * @brief           Attempts a read or write to the given cache
     *
     * @param request   Request structure to attempt
     * @return true     If the request was completed and need not be called
     * again
     */
    Status handleAccess(Request *request);

    /**
     * @brief                       Simulate a clock cycle in the cache
     * structure(s)
     *
     * @param cache                 Main memory cache structure. This function
     * will recursively call all upper caches
     * @param cycle                 Current clock cycle
     * @param completed_requests    Out. An array of the request indices that
     * were completed this tick. Length is the return value
     *
     * @return                      Number of requests completed this tick
     */
    uint64_t internalProcessCache(uint64_t cycle, int16_t *completed_requests);

    // Cache hierarchy fields
    Cache *upper_cache_;
    Cache *lower_cache_;
    Cache *main_memory_;
    CacheLevel cache_level_;

    // Cache sizing fields
    Configuration config_;
    uint64_t num_sets_;

    // Sizing fields used in calculations
    uint64_t block_size_bits_;
    uint64_t block_address_to_set_index_mask_;

    // Data
    Set *sets_;
    RequestManager request_manager_;
    uint64_t cycle_;

    // Data for simulator performance
    uint64_t earliest_next_useful_cycle_;
    bool work_done_this_cycle_;

    Statistics stats_;
};

typedef struct test_params_s {
    uint8_t num_cache_levels;
    uint64_t min_block_size;
    uint64_t max_block_size;
    uint64_t min_cache_size;
    uint64_t max_cache_size;
    uint8_t min_blocks_per_set;
    uint8_t max_blocks_per_set;
    int32_t max_num_threads;
} test_params_t;

inline uint64_t Cache::addressToBlockAddress(uint64_t address) {
    return address >> block_size_bits_;
}

inline uint64_t Cache::blockAddressToSetIndex(uint64_t block_address) {
    return (block_address & block_address_to_set_index_mask_);
}

inline uint64_t Cache::addressToSetIndex(uint64_t address) {
    return blockAddressToSetIndex(addressToBlockAddress(address));
}

inline CacheLevel Cache::GetCacheLevel() { return cache_level_; }

inline Statistics Cache::GetStats() { return stats_; }

inline Configuration Cache::GetConfig() { return config_; }

inline uint64_t Cache::GetCycle() { return cycle_; }
