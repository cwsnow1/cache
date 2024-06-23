#pragma once

#include <atomic>
#include <stdint.h>
#include <stdio.h>
#include <vector>

#include "Cache.h"
#include "Multithreading.h"

class Simulator;

enum CacheType {
    kDataCache,
    kInstructionCache,
    kNumberOfCacheTypes
};

struct SimCacheContext {
    std::vector<Cache*> caches;
    Simulator* pSimulator;
    uint64_t configIndex;
};

class Simulator {
  public:
    Simulator(const char* inputFilename);

    ~Simulator();

#ifdef _MSC_VER
    /**
     * @brief                       Runs through all memory accesses with given setup cache
     *
     * @param pSimCacheContext      void pointer of a SimCacheContext
     *
     * @return                      Status
     */
    static DWORD WINAPI SimCache(void* pSimCacheContext);
#else
    /**
     * @brief                       Runs through all memory accesses with given setup cache
     *
     * @param pSimCacheContext      void pointer of a SimCacheContext
     *
     * @return                      None
     */
    static void* SimCache(void* pSimCacheContext);
#endif

    /**
     * @brief Generate threads that will call sim_cache
     *
     */
    void CreateAndRunThreads();

    /**
     * @brief               Print the statistics gathered in the simulations
     *
     * @param pTextStream   Text file output stream
     * @param pCSVStream    Comma separated value file output stream
     */
    void PrintStats(FILE* pTextStream, FILE* pCSVStream);

#ifdef _MSC_VER
    /**
     * @brief                   Tracks and prints the progress of the simulation, intended to be called from separate
     * thread
     *
     * @param pSimulatorPointer Void pointer to simulator object
     * @return                  Status
     */
    static DWORD WINAPI TrackProgress(void* pSimulatorPointer);
#else
    /**
     * @brief                   Tracks and prints the progress of the simulation, intended to be called from separate
     * thread
     *
     * @param pSimulatorPointer Void pointer to simulator object
     * @return                  None
     */
    static void* TrackProgress(void* pSimulatorPointer);
#endif

    /**
     * @brief Get the number of accesses in trace file
     */
    inline const uint64_t GetNumAccesses() const;

    /**
     * @brief Get accesses, a list of Instruction objects
     */
    inline const MemoryAccesses& GetAccesses() const;

    /**
     * @brief Get the cycle counter
     *
     */
    inline uint64_t& GetCycleCounter(uint64_t index);

    /**
     * @brief Get the threads outstanding
     *
     */
    inline std::vector<Thread_t>& GetThreadsOutstanding();

    /**
     * @brief Decrement the configs to test counter
     *
     */
    void DecrementConfigsToTest();

    /**
     * @brief Decremnet the number of threads outstanding counter
     *
     */
    void DecrementNumThreadsOutstanding();

    Lock_t lock_;

    static constexpr Thread_t kInvalidThreadId = static_cast<Thread_t>(UINT64_MAX);

    static constexpr uint64_t kInvalidRequestIndex = UINT64_MAX;

    static constexpr uint64_t kDataAccessRequest = UINT64_MAX - 1;

    static constexpr uint64_t kProgressTrackerSyncPeriod = 1 << 14;

    static_assert(isPowerOfTwo(kProgressTrackerSyncPeriod), "Sync period must be power of two");

  private:
    /**
     *  @brief Recursive function to tell all cache configs
     * 
     *  @param cacheLevel      the level of the cache this function will try to init
     *  @param minBlockSize     minimum block size this cache level will try to init
     *  @param minCacheSize     minimum cache size this cache level will try to init
     */
    void SetupCaches(CacheLevel cacheLevel, uint64_t minBlockSize, uint64_t minCacheSize);

    // Common across all threads
    MemoryAccesses accesses_;
    std::vector<Thread_t> threads_;
    std::vector<std::vector<std::unique_ptr<Cache>>> caches_;
    std::vector<uint64_t> cycleCounters_;
    std::vector<Thread_t> threadsOutstanding_;
    uint64_t numConfigs_;

    // Note: No performance benefit is seen by limiting the
    // number of outstanding threads. The only benefit is
    // memory savings & keeping the computer usable when
    // running with large numbers of configs
    std::atomic<int32_t> numThreadsOutstanding_;
    uint64_t configsToTest_;
    std::vector<uint64_t> accessIndices_;
};

inline const uint64_t Simulator::GetNumAccesses() const {
    return accesses_.instructionAccesses_.size();
}

inline const MemoryAccesses& Simulator::GetAccesses() const {
    return accesses_;
}

inline uint64_t& Simulator::GetCycleCounter(uint64_t index) {
    return cycleCounters_[index];
}

inline std::vector<Thread_t>& Simulator::GetThreadsOutstanding() {
    return threadsOutstanding_;
}
