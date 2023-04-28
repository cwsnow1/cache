#pragma once

#include "list.h"

constexpr uint64_t kMaxNumberOfRequests = 8;

// Obviously these are approximations
constexpr uint64_t kAccessTimeInCycles[] = {
    3,  // L1
    12, // L2
    38, // L3
    195 // Main Memory
};

enum CacheLevel { kL1, kL2, kL3, kMainMemory } __attribute__((packed));

typedef enum access_e { READ, WRITE } access_t;

enum Status {
    kHit,
    kMiss,
    kWaiting,
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

struct Statistics {
    uint64_t writeHits = 0;
    uint64_t readHits = 0;
    uint64_t writeMisses = 0;
    uint64_t readMisses = 0;
    uint64_t writebacks = 0;
};


struct RequestManager {
    Request *requestPool;
    DoubleList *waitingRequests;
    DoubleList *freeRequests;
    DoubleList *busyRequests;
    uint64_t maxOutstandingRequests;

    /**
     * @brief Initializes the request manager and the request lists it maintains
     * 
     * @param pCacheLevel   The cache level of the cache whose requests this manager manages
     *
     */
    RequestManager (CacheLevel pCacheLevel) {
        maxOutstandingRequests = kMaxNumberOfRequests << pCacheLevel;
        requestPool = new Request[maxOutstandingRequests];
        waitingRequests = new DoubleList(maxOutstandingRequests);
        busyRequests = new DoubleList(maxOutstandingRequests);
        freeRequests = new DoubleList(maxOutstandingRequests);
        for (uint64_t i = 0; i < maxOutstandingRequests; i++) {
            DoubleListElement *element = new DoubleListElement;
            element->pool_index_ = i;
            freeRequests->PushElement(element);
        }
    }

    /**
     * @brief Destroy the Request Manager object
     * 
     */
    ~RequestManager() {
        delete[] requestPool;
        delete waitingRequests;
        delete freeRequests;
        delete busyRequests;
    }

};

class Memory {
   public:

    Memory() = default;

    /**
     * @brief Construct a new Memory object
     * 
     * @param pLowestCache 
     */
    Memory(Memory *pLowestCache);

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
    Memory *GetUpperCache() {
        return upperCache_;
    }

    /**
     * @brief Set the Upper Cache object
     * 
     * @param pUpperCache 
     */
    void SetUpperCache(Memory *pUpperCache) {
        upperCache_ = pUpperCache;
    }

    /**
     * @brief Get the Lower Cache object
     * 
     * @return Memory* memory object one level below this
     */
    Memory *GetLowerCache() {
        return lowerCache_;
    }

    /**
     * @brief Set the Lower Cache object
     * 
     * @param pLowerCache 
     */
    void SetLowerCache(Memory *pLowerCache) {
        lowerCache_ = pLowerCache;
    }

    /**
     * @brief Get the Stats object
     *
     * @return Statistics
     */
    inline Statistics GetStats() { return stats_; }

    /**
     * @brief Get the cache level
     *
     * @return CacheLevel
     */
    inline CacheLevel GetCacheLevel() { return cacheLevel_; }

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
    bool GetWasWorkDoneThisCycle() { return wasWorkDoneThisCycle_; }

    void SetWasWorkDoneThisCycle(bool pWasWorkDoneThisCycle) {
        wasWorkDoneThisCycle_ = pWasWorkDoneThisCycle;
    }

    uint64_t InternalProcessCache (uint64_t pCycle, int16_t *pCompletedRequests);

    /**
     * @brief Get the Earliest Next Useful Cycle
     *
     * @return uint64_t the cycle wherein the next useful piece of work can be
     * done
     */
    uint64_t GetEarliestNextUsefulCycle() { return earliestNextUsefulCycle_; }

    /**
     * @brief Get the Earliest Next Useful Cycle
     *
     * @return uint64_t the cycle wherein the next useful piece of work can be
     * done
     */
    uint64_t CalculateEarliestNextUsefulCycle();

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
     * @brief Get the cycle count
     *
     * @return uint64_t
     */
    inline uint64_t GetCycle() { return cycle_; }

    // Multi-threading fields
    // TODO: move these out of class
    uint64_t thread_id_;
    uint64_t config_index_;

   protected:

    /**
     * @brief           Attempts a read or write to the given cache
     *
     * @param pRequest  Request structure to attempt
     * @return true     If the request was completed and need not be called
     * again
     */
    Status handleAccess(Request *pRequest);

    // Cache hierarchy fields
    Memory *upperCache_;
    Memory *lowerCache_;
    Memory *mainMemory_;
    CacheLevel cacheLevel_;

    uint64_t cycle_;

    RequestManager* requestManager_;

    // Data for simulator performance
    uint64_t earliestNextUsefulCycle_;
    bool wasWorkDoneThisCycle_;

    Statistics stats_;
};
