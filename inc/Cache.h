#pragma once
#include "list.h"
#include "Memory.h"

struct Block {
    // The LSB of the blockAddress will be the
    // setIndex, and would be redudant to store
    uint64_t blockAddress;
    bool dirty;
    bool valid;

    Block() {
        blockAddress = 0;
        dirty = false;
        valid = false;
    }
};

struct Set {
    Block *ways;
    uint8_t *lruList;
    bool busy;

    Set() {
        ways = nullptr;
        lruList = nullptr;
        busy = false;
    }
};

struct Configuration {
    uint64_t cacheSize;
    uint64_t blockSize;
    uint64_t associativity;

    Configuration() = default;
    Configuration(uint64_t pCacheSize, uint64_t pBlockSize,
                  uint64_t pAssociativity) {
        cacheSize = pCacheSize;
        blockSize = pBlockSize;
        associativity = pAssociativity;
    }
};

class Cache : public Memory {
   public:
    Cache() = default;
    /**
     * @brief                       Tries to initialize a cache structure
     *
     * @param pUpperCache           Pointer to cache object higher in cache hierarchy
     * @param pCacheLevel           Level of cache
     * @param pNumberOfCacheLevels  Number of cache levels
     * @param pConfigs              Array of structure containing all the config info needed per cache level
     * @param pConfigIndex          Index in global caches structure
     */
    Cache(Cache *pUpperCache, CacheLevel pCacheLevel, uint8_t pNumberOfCacheLevels,
          Configuration *pConfigs, uint64_t pConfigIndex);

    /**
     * @brief Destroy the Cache object
     *
     */
    ~Cache();

    /**
     * @brief                       Checks whether a given cache config is valid, i.e. not redundant
     *
     * @param pConfig                Structure containing all the config info needed
     * @return true                 if cache config is valid
     */
    static bool IsCacheConfigValid(Configuration pConfig);

    /**
     * @brief       Allocates memory needed for cache struture, recursively
     * calls lower caches
     *
     */
    void AllocateMemory();

    /**
     * @brief           Sets the thread index/ID of the cache instances
     *
     * @param pThreadId Thread ID to be set
     */
    void SetThreadId(uint64_t pThreadId);

    /**
     * @brief       Frees all memory allocated by cache structures, recursively calls all lower caches
     */
    void FreeMemory();

    /**
     * @brief                       Simulate a clock cycle in the cache
     * structure(s)
     *
     * @param pCycle                Current clock cycle
     * @param pCompletedRequests    Out. An array of the request indices that were completed this tick. Length is the return value
     *
     * @return                      Number of requests completed this tick
     */
    uint64_t ProcessCache(uint64_t pCycle, int16_t *pCompletedRequests);

    /**
     * @brief Get the Config object
     *
     * @return Configuration
     */
    inline Configuration GetConfig() { return config_; }

    /**
     * @brief Get the Top Level Cache object
     *
     * @return Cache* top level cache object
     */
    Cache *GetTopLevelCache() {
        Memory *upperCache = GetUpperCache();
        while (upperCache->GetUpperCache() != nullptr) {
            upperCache = upperCache->GetUpperCache();
        }
        return static_cast<Cache*>(upperCache);
    }

    /**
     * @brief Set the Main Memory object
     * 
     * @param pMainMemory Main memory to set
     */
    void SetMainMemory(Memory *pMainMemory) {
        mainMemory_ = pMainMemory;
    }

    /**
     * @brief 
     * 
     * @param pSetIndex 
     */
    void ResetCacheSetBusy(uint64_t pSetIndex);

    uint64_t InternalProcessCache (uint64_t cycle, int16_t *completed_requests);

    /**
     *  @brief Translates a raw address to a set index
     *
     *  @param pAddress     Raw address, 64 bits
     *  @return             Set index
     */
    inline uint64_t addressToSetIndex(uint64_t pAddress);

   private:
    /**
     *  @brief Translates raw address to block address
     *
     *  @param pAddress         Raw address, 64 bits
     *  @return                 Block address, i.e. x MSB of the raw address
     */
    inline uint64_t addressToBlockAddress(uint64_t pAddress);

    /**
     *  @brief Translates a block address to a set index
     *
     *  @param pBlockAddress    Block address, i.e. the raw address shifted
     *  @return                 Set index
     */
    inline uint64_t blockAddressToSetIndex(uint64_t pBlockAddress);

    /**
     * @brief               Reorder the LRU list for the given set
     *
     * @param pSetIndex     Set whose LRU list is to be reordered
     * @param mru_index     Block index that is now the most recently used
     */
    void updateLRUList(uint64_t pSetIndex, uint8_t mru_index);

    /**
     * @brief               Handles the eviction and subsequent interactions
     * with lower cache(s)
     *
     * @param pSetIndex     Set index from which block needs to be evicted
     * @param pBlockAddress Block address that will replace the evicted block
     * @return              Block index within the provided set that the new
     * block occupies, -1 if evict request failed
     */
    int16_t evictBlock(uint64_t pSetIndex, uint64_t pBlockAddress);

    /**
     * @brief               Searches the given set for the given block address
     *
     * @param pSetIndex     Set index to search
     * @param pBlockAddress Block address for which to search
     * @param pBlockIndex   Output: The block index within the set iff found
     * @return true         if the block is found in the set
     */
    bool findBlockInSet(uint64_t pSetIndex, uint64_t pBlockAddress,
                        uint8_t& pBlockIndex);

    /**
     * @brief               Acquires the given block address into the given set.
     * Makes any subsequent calls necessary to lower caches
     *
     * @param pSetIndex     Set in which to put new block
     * @param pBlockAddress    Block address of block to acquire
     * @return              Block index acquired within set, -1 if request
     * failed
     */
    int16_t requestBlock(uint64_t pSetIndex, uint64_t pBlockAddress);

    /**
     * @brief           Attempts a read or write to the given cache
     *
     * @param pRequest  Request structure to attempt
     * @return true     If the request was completed and need not be called
     * again
     */
    Status handleAccess(Request *pRequest);

    /**
     * @brief                       Simulate a clock cycle in the cache structure(s)
     *
     * @param pCycle                Current clock cycle
     * @param pCompletedRequests    Out. An array of the request indices that were completed this tick. Length is the return value
     *
     * @return                      Number of requests completed this tick
     */
    uint64_t internalProcessCache(uint64_t pCycle, int16_t *pCompletedRequests);

    // Cache sizing fields
    Configuration config_;
    uint64_t numSets_;

    // Sizing fields used in calculations
    uint64_t blockSizeBits_;
    uint64_t blockAddressToSetIndexMask_;

    // Data
    Set *sets_;
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
    return address >> blockSizeBits_;
}

inline uint64_t Cache::blockAddressToSetIndex(uint64_t pBlockAddress) {
    return (pBlockAddress & blockAddressToSetIndexMask_);
}

inline uint64_t Cache::addressToSetIndex(uint64_t address) {
    return blockAddressToSetIndex(addressToBlockAddress(address));
}
