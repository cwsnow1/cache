#pragma once

#include <stdint.h>
#include <stdio.h>

#include "Cache.h"


typedef uint64_t bitfield64_t;
class Simulator;

struct SimCacheContext {
    Cache* L1Cache;
    Simulator* simulator;
};

class Simulator {
    public:

    Simulator(const char *input_filename);

    ~Simulator();

    /**
     * @brief                       Runs through all memory accesses with given setup cache
     * 
     * @param pSimCacheContext       void pointer of a SimCacheContext
     */
    static void* SimCache (void *pSimCacheContext);

    /**
     * @brief Generate threads that will call sim_cache
     * 
     */
    void CreateAndRunThreads();

    void PrintStats(FILE* pTextStream, FILE* pCSVStream);

    static void* TrackProgress(void * pObj);

    inline static uint64_t Max(uint64_t x, uint64_t y);
    inline static void SetBit(bitfield64_t& bitfield, int16_t index);
    inline static void ResetBit(bitfield64_t& bitfield, int16_t index);

    inline uint64_t GetNumAccesses();
    inline Instruction* GetAccesses();
    inline uint64_t* GetCycleCounter();
    inline pthread_t* GetThreadsOutstanding();

    void DecrementConfigsToTest();
    void DecrementNumThreadsOutstanding();

    pthread_mutex_t lock_;

    static constexpr uint64_t INVALID_THREAD_ID = UINT64_MAX;

    private:

    /**
     *  @brief Recursive function to tell all cache configs
     * 
     *  @param pCacheLevel      the level of the cache this function will try to init
     *  @param pMinBlockSize    minimum block size this cache level will try to init
     *  @param pMinCacheSize    minimum cache size this cache level will try to init
     */
    void SetupCaches(CacheLevel pCacheLevel, uint64_t pMinBlockSize, uint64_t pMinCacheSize);

    /**
     * @brief                   Recursively calculate the total number of valid cache configs
     * 
     * @param pNumConfigs       Out. Tracks the total number of valid configs
     * @param cache_level       Level of cache the function is in, 0 (L1) when called from without
     * @param min_block_size    Minimum block size as specfied in cache_params.h
     * @param min_cache_size    Minimum cache size as specfied in cache_params.h
     */
    void CalculateNumValidConfigs(uint64_t& pNumConfigs, uint8_t cache_level, uint64_t min_block_size, uint64_t min_cache_size);

    // Common across all threads
    Instruction *accesses_;
    uint64_t numAccesses_;
    pthread_t *threads_;
    Cache **caches_;
    uint64_t *cycleCounter_;
    uint64_t numConfigs_;

    // Note: No performance benefit is seen by limiting the
    // number of outstanding threads. The only benefit is
    // memory savings & keeping the computer usable when
    // running with large numbers of configs
    volatile int32_t numThreadsOutstanding_;
    uint64_t configsToTest_;
    pthread_t* threadsOutstanding_;

};

inline uint64_t Simulator::Max(uint64_t x, uint64_t y) {
    return (x > y) ? x : y;
}

inline void Simulator::SetBit(bitfield64_t& bitfield, int16_t index) {
    bitfield |= (1 << (index));
}

inline void Simulator::ResetBit(bitfield64_t& bitfield, int16_t index) {
    bitfield &= ~(1 << (index));
}

inline uint64_t Simulator::GetNumAccesses() {
    return numAccesses_;
}

inline Instruction* Simulator::GetAccesses() {
    return accesses_;
}

inline uint64_t* Simulator::GetCycleCounter() {
    return cycleCounter_;
}

inline pthread_t* Simulator::GetThreadsOutstanding() {
    return threadsOutstanding_;
}
