#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <chrono>
#include <thread>

#ifdef __GNUC__
#include <pthread.h>
#include <unistd.h>
#endif

#ifdef _MSC_VER
#include <Windows.h>
#endif

#include "Simulator.h"
#include "IOUtilities.h"
#include "Cache.h"
#include "SimTracer.h"
#include "default_test_params.h"
#include "debug.h"
#include "RequestManager.h"
#include "Multithreading.h"

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
    uint8_t *pFileContents = IOUtilities::ReadInFile(pInputFilename, fileLength);
    IOUtilities::ParseBuffer(pFileContents, fileLength, &pAccesses_);

    CalculateNumValidConfigs(numConfigs_, 0, gTestParams.minBlockSize, gTestParams.minCacheSize);
    printf("Total number of possible configs = %" PRIu64 "\n", numConfigs_);
    if (numConfigs_ < static_cast<uint64_t>(gTestParams.maxNumberOfThreads) || (gTestParams.maxNumberOfThreads < 0)) {
        gTestParams.maxNumberOfThreads = numConfigs_;
    }
    configsToTest_ = numConfigs_;
#ifdef _MSC_VER
    if (gTestParams.maxNumberOfThreads > MAXIMUM_WAIT_OBJECTS) {
        gTestParams.maxNumberOfThreads = MAXIMUM_WAIT_OBJECTS;
        printf("Setting maximum number of threads to Windows maximum of %" PRId32 "\n", MAXIMUM_WAIT_OBJECTS);
    }
#endif
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
    pThreads_ = new Thread_t[numConfigs_];
    assert(pThreads_);
    pCaches_ = new Cache*[numConfigs_];
    assert(pCaches_);

    SetupCaches(kL1, gTestParams.minBlockSize, gTestParams.minCacheSize);
}

#ifdef _MSC_VER
DWORD WINAPI Simulator::TrackProgress(void *pSimulatorPointer) {
#else
void* Simulator::TrackProgress(void * pSimulatorPointer) {
#endif
    Simulator *pSimulator = static_cast<Simulator*>(pSimulatorPointer);
    uint64_t numAccesses = static_cast<float> (pSimulator->GetNumAccesses());
    float oneConfigPercentage = 100.0f / static_cast<float> (pSimulator->numConfigs_);
    char progressBar[] = "[                                        ]";
    const int progressBars = sizeof(progressBar) / sizeof(char) - 3;

    printf("Running... %02.0f%% complete\n", 0.0f);
    while(pSimulator->configsToTest_) {
        // Calculate progress
        // 1. Configs completed
        uint64_t configsDone = pSimulator->numConfigs_ - pSimulator->configsToTest_;
        float progressPercent = (configsDone / static_cast<float> (pSimulator->numConfigs_)) * 100.0f;

        // 2. Configs in progress
        for (auto i = 0; i < gTestParams.maxNumberOfThreads; i++) {
            if (pSimulator->pAccessIndices[i] < numAccesses) {
                progressPercent += oneConfigPercentage * (static_cast<float>(pSimulator->pAccessIndices[i]) / numAccesses);
            }
        }

        int numberOfProgressBarCharacters = static_cast<int>(progressBars * (progressPercent / 100.0f));
        for (auto i = 1; i <= numberOfProgressBarCharacters; i++) {
            if (i == numberOfProgressBarCharacters) {
                progressBar[i] = '>';
            } else {
                progressBar[i] = '=';
            }
        }
        // Go back to beginning of line
        printf("\x1b[1A\x1b[1A");
        printf("Running... %02d threads running, %02" PRIu64 " to go. %02.0f%% complete\n", pSimulator->numThreadsOutstanding_, pSimulator->configsToTest_, progressPercent);
        printf("%s\n", progressBar);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // clear screen
    printf("\033[2J");
#ifdef _MSC_VER
    return 0;
#else
    pthread_exit(NULL);
    return nullptr;
#endif
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
    delete pAccesses_;
    delete[] pThreads_;
#if (SIM_TRACE == 1)
    delete gSimTracer;
    printf("Wrote sim trace output to %s\n", SIM_TRACE_FILENAME);
#endif
}

#ifdef _MSC_VER
DWORD WINAPI Simulator::SimCache(void *pSimCacheContext) {
#else
void* Simulator::SimCache (void *pSimCacheContext) {
#endif
    SimCacheContext *simCacheContext = static_cast<SimCacheContext*>(pSimCacheContext);
    Cache *this_cache = simCacheContext->pL1Cache;
    Simulator* pSimulator = simCacheContext->pSimulator;
    uint64_t configIndex = simCacheContext->configIndex;

    uint64_t* cycleCounter = pSimulator->GetCycleCounter();
    MemoryAccesses* accesses = pSimulator->GetAccesses();
    const uint64_t numAccesses = pSimulator->GetNumAccesses();

    assert(this_cache->GetCacheLevel() == kL1);
    this_cache->AllocateMemory();
    cycleCounter[configIndex] = 0;
    uint64_t outstanding_requests[RequestManager::kMaxNumberOfRequests] = { Simulator::kInvalidRequestIndex };
    int16_t completed_requests[RequestManager::kMaxNumberOfRequests] = { 0 };
    static_assert(RequestManager::kMaxNumberOfRequests <= 64,"Too many requests to store requests in this bitfield");
    uint64_t &i = pSimulator->pAccessIndices[this_cache->threadId_];
    DoubleList *pDataAccessRequests = new DoubleList(RequestManager::kMaxNumberOfRequests);
    DoubleList *pFreeAccessRequests = new DoubleList(RequestManager::kMaxNumberOfRequests);
    for (uint64_t requestIndex = 0; requestIndex < RequestManager::kMaxNumberOfRequests; requestIndex++) {
        DoubleListElement *pElement = new DoubleListElement;
        pFreeAccessRequests->PushElement(pElement);
    }
    i = 0;
    bool work_done = false;
    bool isOutstandingRequest;
    do {
#if (CONSOLE_PRINT == 1)
        printf("====================\nTICK %010" PRIu64 "\n====================\n", cycleCounter[configIndex]);
        char c;
        assert_release(scanf("%c", &c) == 1);
#endif
        isOutstandingRequest = false;
        work_done = false;

        if (pDataAccessRequests->PeekHead()) {
            DoubleListElement *pElement = pDataAccessRequests->PeekHead();
            uint64_t instructionIndex = pElement->poolIndex_;
            int16_t request_index = this_cache->AddAccessRequest(accesses->pDataAccesses[instructionIndex], cycleCounter[configIndex]);
            assert(request_index != -1);
            if (request_index != -1) {
                pElement = pDataAccessRequests->PopElement();
                pFreeAccessRequests->PushElement(pElement);
                outstanding_requests[request_index] = Simulator::kDataAccessRequest;
                work_done = true;
            }
            isOutstandingRequest = true;
        }

        if ((i < numAccesses) && !work_done) {
            int16_t request_index = this_cache->AddAccessRequest(accesses->pInstructionAccesses[i], cycleCounter[configIndex]);
            if (request_index != -1) {
                work_done = true;
                outstanding_requests[request_index] = i;
                ++i;
                isOutstandingRequest = true;
            }
        }

        uint64_t num_completed_requests = this_cache->ProcessCache(cycleCounter[configIndex], completed_requests);
        work_done |= this_cache->GetWasWorkDoneThisCycle();
        for (uint64_t j = 0; j < num_completed_requests; j++) {
            work_done = true;
            // Clear out outstanding requests
            if (outstanding_requests[completed_requests[j]] == Simulator::kDataAccessRequest) {
                outstanding_requests[completed_requests[j]] = Simulator::kInvalidRequestIndex;
                continue;
            }
            uint64_t complete_request_index = outstanding_requests[completed_requests[j]];
            outstanding_requests[completed_requests[j]] = Simulator::kInvalidRequestIndex;
            // add data access request to queue
            DoubleListElement *pElement = pFreeAccessRequests->PopElement();
            assert(pElement);
            pElement->poolIndex_ = complete_request_index;
            pDataAccessRequests->AddElementToTail(pElement);

            isOutstandingRequest = true;
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
        if (!isOutstandingRequest) {
            for (uint64_t requestIndex = 0; requestIndex < RequestManager::kMaxNumberOfRequests; requestIndex++) {
                if (outstanding_requests[requestIndex] != Simulator::kInvalidRequestIndex) {
                    isOutstandingRequest = true;
                    break;
                }
            }
        }
    } while (isOutstandingRequest || i < numAccesses);
    CODE_FOR_ASSERT(Statistics stats = this_cache->GetStats());
    assert(stats.readHits + stats.readMisses + stats.writeHits + stats.writeMisses == numAccesses * 2);
    Multithreading::Lock(&pSimulator->lock_);
#if (SIM_TRACE == 1)
    gSimTracer->WriteThreadBuffer(this_cache);
#endif
    pSimulator->DecrementConfigsToTest();
    pSimulator->DecrementNumThreadsOutstanding();
    // Mark thread as not in use
    pSimulator->GetThreadsOutstanding()[this_cache->threadId_] = Simulator::kInvalidThreadId;
    Multithreading::Unlock(&pSimulator->lock_);
    this_cache->FreeMemory();
#ifdef _MSC_VER
    return 0;
#else
    delete pFreeAccessRequests;
    delete pDataAccessRequests;
    pthread_exit(NULL);
    return nullptr;
#endif
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
void Simulator::CreateAndRunThreads (void) {
    Multithreading::InitializeLock(&lock_);

#if (CONSOLE_PRINT == 0)
    Thread_t progressThread;
    Multithreading::StartThread(Simulator::TrackProgress, this, &progressThread);
#endif
    uint64_t threadId = 0;

    // internal threadId to pthread threadId mapping used to track which threads are active
    pThreadsOutstanding_ = new Thread_t[gTestParams.maxNumberOfThreads];
    // Cast of kInvalidThreadId to int is OK for memset because it is all 1
    memset(pThreadsOutstanding_, -1, sizeof(Thread_t) * gTestParams.maxNumberOfThreads);

    pAccessIndices = new uint64_t[gTestParams.maxNumberOfThreads];
    memset(pAccessIndices, 0, sizeof(uint64_t) * gTestParams.maxNumberOfThreads);

    SimCacheContext *pContexts = new SimCacheContext[numConfigs_];
    for (uint64_t i = 0; i < numConfigs_; i++) {
        while (numThreadsOutstanding_ == gTestParams.maxNumberOfThreads)
            ;
        Multithreading::Lock(&lock_);
        numThreadsOutstanding_++;
        // Search for a threadId that is not in use
        for (threadId = 0; threadId < static_cast<uint64_t>(gTestParams.maxNumberOfThreads); threadId++) {
            if (pThreadsOutstanding_[threadId] == kInvalidThreadId) {
                break;
            }
        }
        pCaches_[i]->SetThreadId(threadId);
        pContexts[i].pL1Cache = pCaches_[i];
        pContexts[i].pSimulator = this;
        pContexts[i].configIndex = i;
        Multithreading::StartThread(Simulator::SimCache, static_cast<void*> (&pContexts[i]), &pThreads_[i]);

        pThreadsOutstanding_[threadId] = pThreads_[i];
        Multithreading::Unlock(&lock_);
    }
    Multithreading::WaitForThreads(numConfigs_, pThreads_);

    delete[] pThreadsOutstanding_;
    delete[] pContexts;
    delete[] pAccessIndices;
#if (CONSOLE_PRINT == 0)
    Multithreading::WaitForThreads(1, &progressThread);
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
