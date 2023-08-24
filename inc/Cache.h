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
     * @param cacheLevel            Level of cache
     * @param numberOfCacheLevels   Number of cache levels
     * @param pConfigs              Array of structure containing all the config info needed per cache level
     */
    Cache(Cache *pUpperCache, CacheLevel cacheLevel, uint8_t numberOfCacheLevels, Configuration *pConfigs);

    /**
     * @brief Destroy the Cache object
     *
     */
    ~Cache();

    /**
     * @brief                       Checks whether a given cache config is valid, i.e. not redundant
     *
     * @param config                Structure containing all the config info needed
     * @return true                 if cache config is valid
     */
    static bool IsCacheConfigValid(Configuration config);

    /**
     * @brief       Allocates memory needed for cache struture, recursively calls lower caches
     *
     */
    void AllocateMemory();

    /**
     * @brief           Sets the thread index/ID of the cache instances
     *
     * @param threadId  Thread ID to be set
     */
    void SetThreadId(uint64_t threadId);

    /**
     * @brief       Frees all memory allocated by cache structures, recursively calls all lower caches
     */
    void FreeMemory();

    /**
     * @brief                       Simulate a clock cycle in the cache
     * structure(s)
     *
     * @param cycle                 Current clock cycle
     * @param pCompletedRequests    Out. An array of the request indices that were completed this tick. Length is the return value
     *
     * @return                      Number of requests completed this tick
     */
    uint64_t ProcessCache(uint64_t pCycle, int16_t *pCompletedRequests);

    /**
     * @brief   Get the Config object
     *
     * @return  Configuration
     */
    inline Configuration GetConfig() { return config_; }

    /**
     * @brief   Get the Top Level Cache object
     *
     * @return  Cache* top level cache object
     */
    Cache *GetTopLevelCache() {
        Memory *pUpperCache = GetUpperCache();
        while (pUpperCache->GetUpperCache() != nullptr) {
            pUpperCache = pUpperCache->GetUpperCache();
        }
        return static_cast<Cache*>(pUpperCache);
    }

    /**
     * @brief Set the Main Memory object
     * 
     * @param pMainMemory Main memory to set
     */
    void SetMainMemory(Memory *pMainMemory) {
        pMainMemory_ = pMainMemory;
    }

    /**
     * @brief           Marks a set as no longer busy
     * 
     * @param setIndex  Set to mark as not busy
     */
    void ResetCacheSetBusy(uint64_t setIndex);

    /**
     * @brief                       Simulates a single clock cycle in a single cache level
     * 
     * @param cycle                 Current clock cycle
     * @param pCompletedRequests    Out. An array of the request indices that were completed this tick. Length is the return value
     * 
     * @return                      Number of requests completed this tick
     */
    uint64_t InternalProcessCache (uint64_t cycle, int16_t *pCompletedRequests);

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
     *  @param address          Raw address, 64 bits
     *  @return                 Block address, i.e. x MSB of the raw address
     */
    inline uint64_t addressToBlockAddress(uint64_t address);

    /**
     *  @brief Translates a block address to a set index
     *
     *  @param blockAddress     Block address, i.e. the raw address shifted
     *  @return                 Set index
     */
    inline uint64_t blockAddressToSetIndex(uint64_t blockAddress);

    /**
     * @brief               Reorder the LRU list for the given set
     *
     * @param setIndex      Set whose LRU list is to be reordered
     * @param mruIndex      Block index that is now the most recently used
     */
    void updateLRUList(uint64_t setIndex, uint8_t mruIndex);

    /**
     * @brief               Handles the eviction and subsequent interactions
     * with lower cache(s)
     *
     * @param setIndex      Set index from which block needs to be evicted
     * @return              Block index within the provided set that the new
     * block occupies, -1 if evict request failed
     */
    int16_t evictBlock(uint64_t setIndex);

    /**
     * @brief               Searches the given set for the given block address
     *
     * @param setIndex      Set index to search
     * @param blockAddress  Block address for which to search
     * @param pBlockIndex   Output: The block index within the set iff found
     * @return true         if the block is found in the set
     */
    bool findBlockInSet(uint64_t setIndex, uint64_t blockAddress,
                        uint8_t& pBlockIndex);

    /**
     * @brief               Acquires the given block address into the given set. Makes any subsequent calls necessary to lower caches
     *
     * @param setIndex      Set in which to put new block
     * @param blockAddress  Block address of block to acquire
     * @return              Block index acquired within set, -1 if request failed
     */
    int16_t requestBlock(uint64_t setIndex, uint64_t blockAddress);

    /**
     * @brief           Attempts a read or write to the given cache
     *
     * @param pRequest  Request structure to attempt
     * @return true     If the request was completed and need not be called again
     */
    Status handleAccess(Request *pRequest);

    // Cache sizing fields
    Configuration config_;
    uint64_t numSets_;

    // Sizing fields used in calculations
    uint64_t blockSizeBits_;
    uint64_t blockAddressToSetIndexMask_;

    // Data
    Set *sets_;
};

struct TestParamaters {
    uint8_t numberOfCacheLevels;
    uint64_t minBlockSize[kMaxNumberOfCacheLevels];
    uint64_t maxBlockSize[kMaxNumberOfCacheLevels];
    uint64_t minCacheSize[kMaxNumberOfCacheLevels];
    uint64_t maxCacheSize[kMaxNumberOfCacheLevels];
    uint8_t minBlocksPerSet[kMaxNumberOfCacheLevels];
    uint8_t maxBlocksPerSet[kMaxNumberOfCacheLevels];
    int32_t maxNumberOfThreads;
};

inline uint64_t Cache::addressToBlockAddress(uint64_t address) {
    return address >> blockSizeBits_;
}

inline uint64_t Cache::blockAddressToSetIndex(uint64_t blockAddress) {
    return (blockAddress & blockAddressToSetIndexMask_);
}

inline uint64_t Cache::addressToSetIndex(uint64_t address) {
    return blockAddressToSetIndex(addressToBlockAddress(address));
}
