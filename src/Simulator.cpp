#include <stdint.h>
#include <pthread.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>

#include "Simulator.h"
#include "IOUtilities.h"
#include "Cache.h"
#include "SimTracer.h"
#include "default_test_params.h"
#include "debug.h"
#include "RequestManager.h"

#if (SIM_TRACE == 1)
SimTracer *gSimTracer;
FILE *sim_trace_f;
#endif

TestParamaters gTestParams;


Simulator::Simulator(const char *pInputFilename) {
    numConfigs_ = 0;
    numThreadsOutstanding_ = 0;

    // Look for test parameters file and generate a default if not found
    IOUtilities::LoadTestParameters();

    // Read in trace file
    uint64_t fileLength = 0;
    uint8_t *pFileContents = IOUtilities::ReadInFile(pInputFilename, &fileLength);
    pAccesses_ = IOUtilities::ParseBuffer(pFileContents, fileLength);

    assert(pAccesses_ != NULL);
    numAccesses_ = fileLength / FILE_LINE_LENGTH_IN_BYTES;

    CalculateNumValidConfigs(numConfigs_, 0, gTestParams.minBlockSize, gTestParams.minCacheSize);
    printf("Total number of possible configs = %" PRIu64 "\n", numConfigs_);
    if (numConfigs_ < static_cast<uint64_t>(gTestParams.maxNumberOfThreads) || (gTestParams.maxNumberOfThreads < 0)) {
        gTestParams.maxNumberOfThreads = numConfigs_;
    }
    configsToTest_ = numConfigs_;
#if (SIM_TRACE == 1)
    uint64_t simTraceBufferMemorySize = gTestParams.maxNumberOfThreads * kSimTraceBufferSizeInBytes;
    if (simTraceBufferMemorySize > MEMORY_USAGE_LIMIT) {
        const int32_t newMaxNumberOfThreads = MEMORY_USAGE_LIMIT / kSimTraceBufferSizeInBytes;
        printf("Sim trace buffer memory is too big for %d threads. Lower thread count to %d\n", gTestParams.maxNumberOfThreads, newMaxNumberOfThreads);
        gTestParams.maxNumberOfThreads = newMaxNumberOfThreads;
    }
    gSimTracer = new SimTracer(SIM_TRACE_FILENAME, numConfigs_);
#endif
    pCycleCounter_ = new uint64_t[numConfigs_];
    assert(pCycleCounter_);
    memset(pCycleCounter_, 0, sizeof(uint64_t) * numConfigs_);
    pThreads_ = new pthread_t[numConfigs_];
    assert(pThreads_);
    pCaches_ = new Cache*[numConfigs_];
    assert(pCaches_);

    SetupCaches(kL1, gTestParams.minBlockSize, gTestParams.minCacheSize);
}


void* Simulator::TrackProgress(void * pSimulatorPointer) {
    Simulator *pSimulator = static_cast<Simulator*>(pSimulatorPointer);
    printf("Running... %02.0f%% complete\n", 0.0f);
    while(pSimulator->configsToTest_) {
        uint64_t configsDone = pSimulator->numConfigs_ - pSimulator->configsToTest_;
        float progressPercent = (configsDone / static_cast<float> (pSimulator->numConfigs_)) * 100.0f;
        printf("\x1b[1A");
        printf("Running... %d threads running, %" PRIu64 " to go. %02.0f%% complete\n", pSimulator->numThreadsOutstanding_, pSimulator->configsToTest_, progressPercent);
        sleep(1);
    }
    pthread_exit(NULL);
    return nullptr;
}

void Simulator::PrintStats(FILE* pTextStream, FILE* pCSVStream) {
    float minCpi = static_cast<float> (pCycleCounter_[0]);
    uint64_t min_i = 0;
    if (pCSVStream) {
        for (int i = 0; i < gTestParams.numberOfCacheLevels; i++) {
            fprintf(pCSVStream, "Cache level, Cache size, Block size, Associativity, Num reads, Read miss rate, Num writes, Write miss rate, Total miss rate,");
        }
        fprintf(pCSVStream, "Main memory reads, Main memory writes, Total number of cycles, CPI\n");
    }
    for (uint64_t i = 0; i < numConfigs_; i++) {
        IOUtilities::PrintStatistics(pCaches_[i], pCycleCounter_[i], pTextStream);
        IOUtilities::PrintStatisticsCSV(pCaches_[i], pCycleCounter_[i], pCSVStream);
        Statistics stats = pCaches_[i]->GetStats();
        uint64_t numberOfReads =  stats.readHits  + stats.readMisses;
        uint64_t numberOfWrites = stats.writeHits + stats.writeMisses;
        float cpi = static_cast<float> (pCycleCounter_[i]) / (numberOfReads + numberOfWrites);
        if (cpi < minCpi) {
            minCpi = cpi;
            min_i = i;
        }
    }
    if (pCSVStream) {
        fclose(pCSVStream);
    }
    fprintf(pTextStream, "The config with the lowest CPI of %.4f:\n", minCpi);
    IOUtilities::PrintConfiguration(pCaches_[min_i], pTextStream);
}

Simulator::~Simulator() {
    for (uint64_t i = 0; i < numConfigs_; i++) {
        delete pCaches_[i];
    }
    delete[] pCaches_;
    delete[] pCycleCounter_;
    delete[] pAccesses_;
    delete[] pThreads_;
#if (SIM_TRACE == 1)
    delete gSimTracer;
    printf("Wrote sim trace output to %s\n", SIM_TRACE_FILENAME);
#endif
}

void* Simulator::SimCache (void *pSimCacheContext) {
    SimCacheContext *simCacheContext = static_cast<SimCacheContext*>(pSimCacheContext);
    Cache *this_cache = simCacheContext->pL1Cache;
    Simulator* pSimulator = simCacheContext->pSimulator;
    uint64_t configIndex = simCacheContext->configIndex;

    uint64_t* cycleCounter = pSimulator->GetCycleCounter();
    Instruction* accesses = pSimulator->GetAccesses();
    const uint64_t numAccesses = pSimulator->GetNumAccesses();

    assert(this_cache->GetCacheLevel() == kL1);
    this_cache->AllocateMemory();
    cycleCounter[configIndex] = 0;
    bitfield64_t oustanding_requests = 0;
    int16_t completed_requests[RequestManager::kMaxNumberOfRequests] = { 0 };
    static_assert(RequestManager::kMaxNumberOfRequests <= 64,"Too many requests to store requests in this bitfield");
    uint64_t i = 0;
    bool work_done = false;
    do {
#if (CONSOLE_PRINT == 1)
        printf("====================\nTICK %010" PRIu64 "\n====================\n", cycleCounter[configIndex]);
        char c;
        assert_release(scanf("%c", &c) == 1);
#endif
        work_done = false;
        if (i < numAccesses) {
            int16_t request_index = this_cache->AddAccessRequest(accesses[i], cycleCounter[configIndex]);
            if (request_index != -1) {
                work_done = true;
                ++i;
                Simulator::SetBit(oustanding_requests, request_index);
            }
        }
        uint64_t num_completed_requests = this_cache->ProcessCache(cycleCounter[configIndex], completed_requests);
        work_done |= this_cache->GetWasWorkDoneThisCycle();
        for (uint64_t j = 0; j < num_completed_requests; j++) {
            work_done = true;
            Simulator::ResetBit(oustanding_requests, completed_requests[j]);
        }
        if (work_done) {
            cycleCounter[configIndex]++;
        } else {
            uint64_t earliestNextUsefulCycle = this_cache->CalculateEarliestNextUsefulCycle();
            assert(earliestNextUsefulCycle > cycleCounter[configIndex]);
            if (earliestNextUsefulCycle < UINT64_MAX) {
#if (CONSOLE_PRINT == 1)
                printf("Skipping to earliest next useful cycle = %" PRIu64 "\n", earliestNextUsefulCycle);
#endif
                cycleCounter[configIndex] = earliestNextUsefulCycle;
            } else {
                cycleCounter[configIndex]++;
            }
        }
    } while (oustanding_requests || i < numAccesses);
    pthread_mutex_lock(&pSimulator->lock_);
#if (SIM_TRACE == 1)
    gSimTracer->WriteThreadBuffer(this_cache);
#endif
    pSimulator->DecrementConfigsToTest();
    pSimulator->DecrementNumThreadsOutstanding();
    assert(pSimulator->GetThreadsOutstanding()[this_cache->threadId_] == pthread_self());
    // Mark thread as not in use
    pSimulator->GetThreadsOutstanding()[this_cache->threadId_] = Simulator::kInvalidThreadId;
    pthread_mutex_unlock(&pSimulator->lock_);
    this_cache->FreeMemory();
    pthread_exit(NULL);
    return nullptr;
}

void Simulator::DecrementConfigsToTest() {
    configsToTest_--;
}

void Simulator::DecrementNumThreadsOutstanding() {
    numThreadsOutstanding_--;
}

/**
 * @brief Generate threads that will call sim_cache
 * 
 */
void Simulator::CreateAndRunThreads (void) {\
    if (pthread_mutex_init(&lock_, NULL) != 0){
        fprintf(stderr, "Mutex lock init failed\n");
        exit(1);
    }
#if (CONSOLE_PRINT == 0)
    pthread_t progress_thread;
    pthread_create(&progress_thread, NULL, Simulator::TrackProgress, this);
#endif
    uint64_t threadId = 0;

    // internal threadId to pthread threadId mapping used to track which threads are active
    pThreadsOutstanding_ = new pthread_t[gTestParams.maxNumberOfThreads];
    // Cast of kInvalidThreadId to int is OK for memset because it is all 1
    memset(pThreadsOutstanding_, static_cast<int> (kInvalidThreadId), sizeof(pthread_t) * gTestParams.maxNumberOfThreads);
    SimCacheContext *contexts = new SimCacheContext[numConfigs_];
    for (uint64_t i = 0; i < numConfigs_; i++) {
        while (numThreadsOutstanding_ == gTestParams.maxNumberOfThreads)
            ;
        pthread_mutex_lock(&lock_);
        numThreadsOutstanding_++;
        // Search for a threadId that is not in use
        for (threadId = 0; threadId < static_cast<uint64_t>(gTestParams.maxNumberOfThreads); threadId++) {
            if (pThreadsOutstanding_[threadId] == kInvalidThreadId) {
                break;
            }
        }
        pCaches_[i]->SetThreadId(threadId);
        contexts[i].pL1Cache = pCaches_[i];
        contexts[i].pSimulator = this;
        contexts[i].configIndex = i;
        if (pthread_create(&pThreads_[i], NULL, Simulator::SimCache, static_cast<void*>(&contexts[i]))) {
            fprintf(stderr, "Error in creating thread %" PRIu64 "\n", i);
        }
        pThreadsOutstanding_[threadId] = pThreads_[i];
        pthread_mutex_unlock(&lock_);
    }
    for (uint64_t i = 0; i < numConfigs_; i++) {
        pthread_join(pThreads_[i], NULL);
    }
    delete[] pThreadsOutstanding_;
#if (CONSOLE_PRINT == 0)
    pthread_join(progress_thread, NULL);
#endif
    assert(numThreadsOutstanding_ == 0);
}

void Simulator::SetupCaches (CacheLevel cacheLevel, uint64_t minBlockSize, uint64_t minCacheSize) {
    static uint64_t threadNumber = 0;
    static Configuration configs[MAX_NUM_CACHE_LEVELS];
    for (uint64_t blockSize = minBlockSize; blockSize <= gTestParams.maxBlockSize; blockSize <<= 1) {
        for (uint64_t cacheSize = Max(minCacheSize, blockSize); cacheSize <= (gTestParams.maxCacheSize * (1 << cacheLevel)); cacheSize <<= 1) {
            for (uint8_t blocksPerSet = gTestParams.minBlocksPerSet; blocksPerSet <= gTestParams.maxBlocksPerSet; blocksPerSet <<= 1) {
                configs[cacheLevel].blockSize = blockSize;
                configs[cacheLevel].cacheSize = cacheSize;
                configs[cacheLevel].associativity = blocksPerSet;
                if (Cache::IsCacheConfigValid(configs[cacheLevel])) {
                    if (cacheLevel < gTestParams.numberOfCacheLevels - 1) {
                        SetupCaches(static_cast<CacheLevel>(cacheLevel + 1), blockSize, cacheSize * 2);
                    } else {
                        pCaches_[threadNumber] = new Cache(NULL, kL1, gTestParams.numberOfCacheLevels, configs);
                        threadNumber++;
                    }
                }
            }
        }
    }
}

void Simulator::CalculateNumValidConfigs (uint64_t& pNumConfigs, uint8_t cacheLevel, uint64_t minBlockSize, uint64_t minCacheSize) {
    for (uint64_t blockSize = minBlockSize; blockSize <= gTestParams.maxBlockSize; blockSize <<= 1) {
        for (uint64_t cacheSize = Max(minCacheSize, blockSize); cacheSize <= gTestParams.maxCacheSize; cacheSize <<= 1) {
            for (uint8_t blocksPerSet = gTestParams.minBlocksPerSet; blocksPerSet <= gTestParams.maxBlocksPerSet; blocksPerSet <<= 1) {
                Configuration config = Configuration(cacheSize, blockSize, blocksPerSet);
                if (Cache::IsCacheConfigValid(config)) {
                    if (cacheLevel < gTestParams.numberOfCacheLevels - 1) {
                        CalculateNumValidConfigs(pNumConfigs, cacheLevel + 1, blockSize, cacheSize);
                    } else {
                        pNumConfigs++;
                    }
                }
            }
        }
    }
}
