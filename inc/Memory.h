#pragma once

#include "GlobalIncludes.h"
#include "RequestManager.h"
#include "list.h"
#include <memory>

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
    Memory() = delete;
    Memory(const Memory&) = delete;
    Memory operator=(const Memory&) = delete;

    /**
     * @brief Construct a new Memory object
     *
     * @param pLowestCache
     */
    Memory(Memory* pLowestCache, CacheLevel cacheLevel);

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
    Memory* GetUpperCache() const {
        return pUpperCache_;
    }

    /**
     * @brief Get the Lower Cache object
     *
     * @return Memory* memory object one level below this
     */
    Memory& GetLowerCache() const {
        return *pLowerCache_;
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
     * @brief Get a readonly reference to the Stats object
     *
     * @return Statistics
     */
    inline const Statistics& ViewStats() const {
        return stats_;
    }

    /**
     * @brief Get the cache level
     *
     * @return CacheLevel
     */
    inline CacheLevel GetCacheLevel() const {
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
    inline bool GetWasWorkDoneThisCycle() const {
        return wasWorkDoneThisCycle_;
    }

    /**
     * @brief                       Set whether work was done this cycle
     *
     * @param wasWorkDoneThisCycle  whether a meaningful piece of work was done such that the upper cache needs to check
     * its busy list
     */
    inline void SetWasWorkDoneThisCycle(bool wasWorkDoneThisCycle) {
        wasWorkDoneThisCycle_ = wasWorkDoneThisCycle;
    }

    /**
     * @brief                       Simulates a single clock cycle in a single cache level
     *
     * @param cycle                 Current clock cycle
     * @param completedRequests     Out. A vector of the request indices that were completed this tick.
     *
     * @return                      None
     */
    void InternalProcessCache(uint64_t cycle, std::vector<int16_t>& completedRequests);

    /**
     * @brief Get the Earliest Next Useful Cycle
     *
     * @return uint64_t the cycle wherein the next useful piece of work can be done
     */
    inline uint64_t GetEarliestNextUsefulCycle() const {
        return earliestNextUsefulCycle_;
    }

    /**
     * @brief Get the Earliest Next Useful Cycle
     *
     * @return uint64_t the cycle wherein the next useful piece of work can be done
     */
    uint64_t CalculateEarliestNextUsefulCycle() const;

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
    Status handleAccess(Request& pRequest);

    // Cache hierarchy fields
    Memory* const pUpperCache_;
    std::unique_ptr<Memory> pLowerCache_;
    Memory* pMainMemory_;
    CacheLevel cacheLevel_;

    uint64_t cycle_ = 0;

    std::unique_ptr<RequestManager> pRequestManager_;

    // Data for simulator performance
    uint64_t earliestNextUsefulCycle_;
    bool wasWorkDoneThisCycle_;

    Statistics stats_;
};
