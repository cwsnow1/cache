#pragma once

#include <stdint.h>
#include <stdio.h>

#include "Multithreading.h"
#include "Cache.h"


typedef uint64_t bitfield64_t;
class Simulator;

enum CacheType {
    kDataCache,
    kInstructionCache,
    kNumberOfCacheTypes
};

struct CacheGroup {
    Cache* pCaches[kNumberOfCacheTypes];

    ~CacheGroup() {
        for (auto i = 0; i < kNumberOfCacheTypes; i++) {
            delete pCaches[i];
        }
    }
};

struct SimCacheContext {
    CacheGroup *caches;
    Simulator* pSimulator;
    uint64_t configIndex;
};

class Simulator {
    public:

    Simulator(const char *inputFilename);

    ~Simulator();

#ifdef _MSC_VER
    /**
     * @brief                       Runs through all memory accesses with given setup cache
     * 
     * @param pSimCacheContext      void pointer of a SimCacheContext
     * 
     * @return                      Status
     */
    static DWORD WINAPI SimCache (void *pSimCacheContext);
#else
    /**
     * @brief                       Runs through all memory accesses with given setup cache
     * 
     * @param pSimCacheContext      void pointer of a SimCacheContext
     * 
     * @return                      None
     */
    static void* SimCache (void *pSimCacheContext);
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
     * @brief                   Tracks and prints the progress of the simulation, intended to be called from separate thread
     * 
     * @param pSimulatorPointer Void pointer to simulator object
     * @return                  Status
     */
    static DWORD WINAPI TrackProgress(void *pSimulatorPointer);
#else
    /**
     * @brief                   Tracks and prints the progress of the simulation, intended to be called from separate thread
     * 
     * @param pSimulatorPointer Void pointer to simulator object
     * @return                  None
     */
    static void* TrackProgress(void * pSimulatorPointer);
#endif

    /**
     * @brief   Determines the maximum value of given parameters
     * 
     * @param x 
     * @param y 
     * @return The greater of x or y
     */
    inline static uint64_t Max(uint64_t x, uint64_t y);

    /**
     * @brief           Set the bit in given bitfield
     * 
     * @param pBitfield Bitfield to be set
     * @param index     Index of bit to set
     */
    inline static void SetBit(bitfield64_t& pBitfield, int16_t index);

    /**
     * @brief           Reset the bit in given bitfield
     * 
     * @param pBitfield Bitfield to be reset
     * @param index     Index of bit to reset
     */
    inline static void ResetBit(bitfield64_t& pBitfield, int16_t index);

    /**
     * @brief Get the number of accesses in trace file
     */
    inline uint64_t GetNumAccesses();

    /**
     * @brief Get accesses, a list of Instruction objects
     */
    inline MemoryAccesses* GetAccesses();

    /**
     * @brief Get the cycle counter
     * 
     */
    inline uint64_t* GetCycleCounter();

    /**
     * @brief Get the threads outstanding
     * 
     */
    inline Thread_t* GetThreadsOutstanding();

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

    static constexpr Thread_t kInvalidThreadId = reinterpret_cast<Thread_t>(UINT64_MAX);

    static constexpr uint64_t kInvalidRequestIndex = UINT64_MAX;

    static constexpr uint64_t kDataAccessRequest = UINT64_MAX - 1;

    private:

    /**
     *  @brief Recursive function to tell all cache configs
     * 
     *  @param pCacheLevel      the level of the cache this function will try to init
     *  @param minBlockSize     minimum block size this cache level will try to init
     *  @param minCacheSize     minimum cache size this cache level will try to init
     */
    void SetupCaches(CacheLevel pCacheLevel, uint64_t minBlockSize, uint64_t minCacheSize);

    /**
     * @brief                   Recursively calculate the total number of valid cache configs
     * 
     * @param pNumConfigs       Out. Tracks the total number of valid configs
     * @param cacheLevel        Level of cache the function is in, 0 (L1) when called from without
     * @param minBlockSize      Minimum block size as specfied in cache_params.h
     * @param minCacheSize      Minimum cache size as specfied in cache_params.h
     */
    void CalculateNumValidConfigs(uint64_t& pNumConfigs, uint8_t cacheLevel, uint64_t minBlockSize, uint64_t minCacheSize);

    // Common across all threads
    MemoryAccesses *pAccesses_;
    Thread_t *pThreads_;
    Thread_t *pThreadsOutstanding_;
    CacheGroup *pCaches_;
    uint64_t *pCycleCounter_;
    uint64_t numConfigs_;

    // Note: No performance benefit is seen by limiting the
    // number of outstanding threads. The only benefit is
    // memory savings & keeping the computer usable when
    // running with large numbers of configs
    volatile int32_t numThreadsOutstanding_;
    uint64_t configsToTest_;
    uint64_t *pAccessIndices;

};

inline uint64_t Simulator::Max(uint64_t x, uint64_t y) {
    return (x > y) ? x : y;
}

inline void Simulator::SetBit(bitfield64_t& bitfield, int16_t index) {
    bitfield |= (1ULL << (index));
}

inline void Simulator::ResetBit(bitfield64_t& bitfield, int16_t index) {
    bitfield &= ~(1ULL << (index));
}

inline uint64_t Simulator::GetNumAccesses() {
    return pAccesses_->length;
}

inline MemoryAccesses* Simulator::GetAccesses() {
    return pAccesses_;
}

inline uint64_t* Simulator::GetCycleCounter() {
    return pCycleCounter_;
}

inline Thread_t* Simulator::GetThreadsOutstanding() {
    return pThreadsOutstanding_;
}
