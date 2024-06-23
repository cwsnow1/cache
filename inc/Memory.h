#pragma once

#include "GlobalIncludes.h"
#include "RequestManager.h"
#include "list.h"

// Obviously these are approximations
constexpr uint64_t kAccessTimeInCycles[] = {
    3,  // L1
    12, // L2
    38, // L3
    195 // Main Memory
};

enum Status {
    kHit,
    kMiss,
    kWaiting,
    kBusy,
};

struct Statistics {
    uint64_t writeHits = 0;
    uint64_t readHits = 0;
    uint64_t writeMisses = 0;
    uint64_t readMisses = 0;
    uint64_t writebacks = 0;
    uint64_t numInstructions = 0;
};

class Memory {
  public:
    Memory() = default;

    /**
     * @brief Construct a new Memory object
     *
     * @param pLowestCache
     */
    Memory(Memory* pLowestCache);

    /**
     * @brief Destroy the Memory object
     *
     */
    ~Memory() = default;

    /**
     * @brief       Allocates memory needed for memory object
     *
     */
    void AllocateMemory();

    /**
     * @brief Get the Upper Cache object
     *
     * @return Memory* memory object one level above this
     */
    Memory* GetUpperCache() {
        return pUpperCache_;
    }

    /**
     * @brief Set the Upper Cache object
     *
     * @param pUpperCache
     */
    void SetUpperCache(Memory* pUpperCache) {
        pUpperCache_ = pUpperCache;
    }

    /**
     * @brief Get the Lower Cache object
     *
     * @return Memory* memory object one level below this
     */
    Memory* GetLowerCache() {
        return pLowerCache_;
    }

    /**
     * @brief Set the Lower Cache object
     *
     * @param pLowerCache
     */
    void SetLowerCache(Memory* pLowerCache) {
        pLowerCache_ = pLowerCache;
    }

    /**
     * @brief Get the Stats object
     *
     * @return Statistics
     */
    inline Statistics& GetStats() {
        return stats_;
    }

    /**
     * @brief Get the cache level
     *
     * @return CacheLevel
     */
    inline CacheLevel GetCacheLevel() {
        return cacheLevel_;
    }

    /**
     * @brief Frees memory allocated by this Memory object
     *
     */
    void FreeMemory();

    /**
     * @brief Determines whether any useful work was done in the last cycle
     *
     * @return true if work was done
     * @return false id work wasn't done
     */
    bool GetWasWorkDoneThisCycle() {
        return wasWorkDoneThisCycle_;
    }

    /**
     * @brief                       Set whether work was done this cycle
     *
     * @param wasWorkDoneThisCycle  whether a meaningful piece of work was done such that the upper cache needs to check
     * its busy list
     */
    void SetWasWorkDoneThisCycle(bool wasWorkDoneThisCycle) {
        wasWorkDoneThisCycle_ = wasWorkDoneThisCycle;
    }

    /**
     * @brief                       Simulates a single clock cycle in a single cache level
     *
     * @param cycle                 Current clock cycle
     * @param pCompletedRequests    Out. An array of the request indices that were completed this tick. Length is the
     * return value
     *
     * @return                      Number of requests completed this tick
     */
    uint64_t InternalProcessCache(uint64_t cycle, int16_t* pCompletedRequests);

    /**
     * @brief Get the Earliest Next Useful Cycle
     *
     * @return uint64_t the cycle wherein the next useful piece of work can be done
     */
    uint64_t GetEarliestNextUsefulCycle() {
        return earliestNextUsefulCycle_;
    }

    /**
     * @brief Get the Earliest Next Useful Cycle
     *
     * @return uint64_t the cycle wherein the next useful piece of work can be done
     */
    uint64_t CalculateEarliestNextUsefulCycle();

    /**
     * @brief Simulates a read or write to an address
     *
     * @param access    Instruction struct, comprises an address and access type (R/W)
     * @param cycle     Current clock cycle
     *
     * @return          The index of the added request, -1 if request add failed
     */
    int16_t AddAccessRequest(Instruction access, uint64_t cycle);

    /**
     * @brief Get the cycle count
     *
     * @return uint64_t
     */
    inline uint64_t GetCycle() {
        return cycle_;
    }

    // Multi-threading field
    uint64_t threadId_;

  protected:
    /**
     * @brief           Attempts a read or write to the given cache
     *
     * @param pRequest  Request structure to attempt
     * @return true     If the request was completed and need not be called
     * again
     */
    Status handleAccess(Request* pRequest);

    // Cache hierarchy fields
    Memory* pUpperCache_;
    Memory* pLowerCache_;
    Memory* pMainMemory_;
    CacheLevel cacheLevel_;

    uint64_t cycle_;

    RequestManager* pRequestManager_;

    // Data for simulator performance
    uint64_t earliestNextUsefulCycle_;
    bool wasWorkDoneThisCycle_;

    Statistics stats_;
};
