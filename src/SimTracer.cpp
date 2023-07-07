#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

#include "Cache.h"
#include "SimTracer.h"
#include "debug.h"
#if (SIM_TRACE == 1)

#include "sim_trace_decoder.h"

extern TestParamaters gTestParams;

SimTracer::SimTracer(const char *filename, uint64_t numberOfConfigs) {
    if (numberOfConfigs > SIM_TRACE_WARNING_THRESHOLD) {
        printf("The number of configs is very high for simulation tracing.\n");
        printf("There is no issue with that, but it will take ~2 times as long\n");
        printf("as normal, and will write a .bin file that will be %lu MiB\n\n", (numberOfConfigs * kSimTraceBufferSizeInBytes) >> 20);
        printf("Note: On my setup, making the number of threads unlimited has a\n");
        printf("bigger benefit to performance when sim tracing than when not.\n\n");
        printf("Do you wish to continue? [Y/n]\n");
        char response;
        assert_release(scanf("%c", &response) == 1);
        if (response != 'Y') {
            exit(0);
        }
    }
    assert(numberOfConfigs);

    pFile_ = fopen(filename, "wb");
    if (pFile_ == NULL) {
        fprintf(stderr, "Error in opening sim trace file\n");
        exit(1);
    }
    CODE_FOR_ASSERT(size_t ret = 0);
    // uint32_t number of bytes in each sim_trace
    const uint32_t sim_trace_buffer_size_in_bytes =  kSimTraceBufferSizeInBytes;
    CODE_FOR_ASSERT(ret =) fwrite(&sim_trace_buffer_size_in_bytes, sizeof(uint32_t), 1, pFile_);
    assert(ret == 1);
    // uint16_t number of configs
    CODE_FOR_ASSERT(ret =) fwrite(&numberOfConfigs, sizeof(uint16_t), 1, pFile_);
    assert(ret == 1);
    CODE_FOR_ASSERT(ret =) fwrite(&gTestParams.numberOfCacheLevels, sizeof(uint8_t), 1, pFile_);
    assert(ret == 1);

    assert(kSimTraceBufferSizeInBytes <= UINT32_MAX);
    pBufferAppendPoints_ = new uint8_t*[gTestParams.maxNumberOfThreads];
    pSimTraceBuffer_ = new uint8_t*[gTestParams.maxNumberOfThreads];
    pEntryCounters_ = new uint64_t[gTestParams.maxNumberOfThreads]();
    pPreviousCycleCounter_ = new uint64_t[gTestParams.maxNumberOfThreads]();
    for (uint64_t i = 0; i < static_cast<uint64_t>(gTestParams.maxNumberOfThreads); i++) {
        pSimTraceBuffer_[i] = new uint8_t[kSimTraceBufferSizeInBytes]();
        pBufferAppendPoints_[i] = pSimTraceBuffer_[i];
        assert(pSimTraceBuffer_[i]);
        memset(pSimTraceBuffer_[i], SIM_TRACE__INVALID, kSimTraceBufferSizeInBytes);
    }
}

SimTracer::~SimTracer() {
    for (uint16_t i = 0; i < gTestParams.maxNumberOfThreads; i++) {
        delete[] pSimTraceBuffer_[i];
    }
    delete[] pBufferAppendPoints_;
    delete[] pEntryCounters_;
    delete[] pPreviousCycleCounter_;
    delete[] pSimTraceBuffer_;
    fclose(pFile_);
}

void SimTracer::Print(trace_entry_id_t traceEntryId, Memory *pMemory, ...) {
    uint64_t threadId = pMemory->threadId_;
    assert(threadId < static_cast<uint64_t>(gTestParams.maxNumberOfThreads));
    uint64_t cycle = pMemory->GetCycle();
    CacheLevel cacheLevel = pMemory->GetCacheLevel();
    // Roll over when buffer is filled
    uint8_t *pLastEntry = pSimTraceBuffer_[threadId] + SIM_TRACE_LAST_ENTRY_OFFSET;
    if (pBufferAppendPoints_[threadId] >= pLastEntry) {
        pBufferAppendPoints_[threadId] = pSimTraceBuffer_[threadId];
    }
    // Sync pattern if needed
    if (++pEntryCounters_[threadId] == SIM_TRACE_SYNC_INTERVAL) {
        *((sync_pattern_t*)pBufferAppendPoints_[threadId]) = kSyncPattern;
        pBufferAppendPoints_[threadId] += sizeof(sync_pattern_t);
        pEntryCounters_[threadId] = 0;
    }
    if (cycle - pPreviousCycleCounter_[threadId] > UINT16_MAX) {
        fprintf(stderr, "cycle offset overflow!\n");
    }
    uint16_t cycleOffset = (uint8_t)(cycle - pPreviousCycleCounter_[threadId]);
    pPreviousCycleCounter_[threadId] = cycle;
    SimTraceEntry entry = SimTraceEntry(cycleOffset, traceEntryId, cacheLevel);
    *((SimTraceEntry*) pBufferAppendPoints_[threadId]) = entry;
    pBufferAppendPoints_[threadId] += sizeof(SimTraceEntry);
    va_list values;
    va_start(values, pMemory);
    for (uint8_t i = 0; i < kNumberOfArgumentsInSimTraceEntry[traceEntryId]; i++) {
        *((sim_trace_entry_data_t*)pBufferAppendPoints_[threadId]) = va_arg(values, sim_trace_entry_data_t);
        pBufferAppendPoints_[threadId] += sizeof(sim_trace_entry_data_t);
    }
    va_end(values);
}

/**
 * File format is as follows:
 * uint32_t buffer size in bytes
 * uint16_t number of configs
 * uint8_t num_cache_levels
 * Config entries
 * 
 * Config entry is as follows:
 * uint32_t buffer append point offset
 * config of each cache
 * buffer
 */

void SimTracer::WriteThreadBuffer(Cache *pCache) {
    CODE_FOR_ASSERT(size_t ret = 0);
    uint64_t threadId = pCache->threadId_;

    uint32_t bufferAppendPointOffset = (uint32_t)(pBufferAppendPoints_[threadId] - pSimTraceBuffer_[threadId]);
    CODE_FOR_ASSERT(ret =) fwrite(&bufferAppendPointOffset, sizeof(uint32_t), 1, pFile_);
    assert(ret == 1);
    // configs of each cache
    Configuration *pConfigs = new Configuration[gTestParams.numberOfCacheLevels];
    uint8_t i = 0;
    for (Cache *pCacheIterator = pCache; pCacheIterator->GetCacheLevel() != kMainMemory; pCacheIterator = static_cast<Cache*>(pCacheIterator->GetLowerCache()), i++) {
        pConfigs[i].cacheSize = pCacheIterator->GetConfig().cacheSize;
        pConfigs[i].blockSize = pCacheIterator->GetConfig().blockSize;
        pConfigs[i].associativity = pCacheIterator->GetConfig().associativity;
    }
    CODE_FOR_ASSERT(ret =) fwrite(pConfigs, sizeof(Configuration), gTestParams.numberOfCacheLevels, pFile_);
    assert(ret == gTestParams.numberOfCacheLevels);
    delete[] pConfigs;
    CODE_FOR_ASSERT(ret =) fwrite(pSimTraceBuffer_[threadId], sizeof(uint8_t), kSimTraceBufferSizeInBytes, pFile_);
    assert(ret == kSimTraceBufferSizeInBytes);

    // Reset this thread's buffer for next config to use
    pBufferAppendPoints_[threadId] = pSimTraceBuffer_[threadId];
    pEntryCounters_[threadId] = 0;
    pPreviousCycleCounter_[threadId] = 0;
    memset(pSimTraceBuffer_[threadId], SIM_TRACE__INVALID, kSimTraceBufferSizeInBytes);
}

#endif
