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

    CalculateNumValidConfigs(numConfigs_, 0, gTestParams.minBlockSize[kL1], gTestParams.minCacheSize[kL1]);
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
    pCaches_ = new CacheGroup[numConfigs_];
    assert(pCaches_);

    SetupCaches(kL1, gTestParams.minBlockSize[kL1], gTestParams.minCacheSize[kL1]);
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
        IOUtilities::PrintStatistics(pCaches_[i].pCaches[kDataCache], pCycleCounter_[i], pTextStream);
        IOUtilities::PrintStatisticsCSV(pCaches_[i].pCaches[kDataCache], pCycleCounter_[i], pCSVStream);
        Statistics stats = pCaches_[i].pCaches[kDataCache]->GetStats();
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
    IOUtilities::PrintConfiguration(pCaches_[min_i].pCaches[kDataCache], pTextStream);
}

Simulator::~Simulator() {
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
    Cache* pTheseCaches[kNumberOfCacheTypes];
    for (auto i = 0; i < kNumberOfCacheTypes; i++) {
        pTheseCaches[i] = simCacheContext->caches->pCaches[i];
        assert(pTheseCaches[i]->GetCacheLevel() == kL1);
        pTheseCaches[i]->AllocateMemory();
    }
    Simulator* pSimulator = simCacheContext->pSimulator;
    uint64_t configIndex = simCacheContext->configIndex;

    uint64_t* cycleCounter = pSimulator->GetCycleCounter();
    MemoryAccesses* accesses = pSimulator->GetAccesses();
    const uint64_t numAccesses = pSimulator->GetNumAccesses();

    uint64_t localCycleCounter = 0;
    uint64_t outstanding_requests[kNumberOfCacheTypes][RequestManager::kMaxNumberOfRequests] = { Simulator::kInvalidRequestIndex };
    int16_t completed_requests[kNumberOfCacheTypes][RequestManager::kMaxNumberOfRequests] = { 0 };
    uint64_t num_completed_requests[kNumberOfCacheTypes] = { 0 };

    DoubleList *pDataAccessRequests = new DoubleList(RequestManager::kMaxNumberOfRequests);
    DoubleList *pFreeAccessRequests = new DoubleList(RequestManager::kMaxNumberOfRequests);
    uint64_t reservedCount = 0;
    for (uint64_t requestIndex = 0; requestIndex < RequestManager::kMaxNumberOfRequests; requestIndex++) {
        DoubleListElement *pElement = new DoubleListElement;
        pFreeAccessRequests->PushElement(pElement);
    }
    uint64_t i = 0;
    bool work_done = false;
    bool isOutstandingRequest;
    do {
#if (CONSOLE_PRINT == 1)
        printf("====================\nTICK %010" PRIu64 "\n====================\n", localCycleCounter);
        char c;
        assert_release(scanf("%c", &c) == 1);
#endif
        isOutstandingRequest = false;
        work_done = false;

        if (pDataAccessRequests->PeekHead()) {
            DoubleListElement *pElement = pDataAccessRequests->PeekHead();
            uint64_t instructionIndex = pElement->poolIndex_;
            int16_t request_index = pTheseCaches[kDataCache]->AddAccessRequest(accesses->pDataAccesses[instructionIndex], localCycleCounter);
            if (request_index != RequestManager::kInvalidRequestIndex) {
                pElement = pDataAccessRequests->PopElement();
                pFreeAccessRequests->PushElement(pElement);
                outstanding_requests[kDataCache][request_index] = Simulator::kDataAccessRequest;
                work_done = true;
            }
            isOutstandingRequest = true;
        }

        if ((i < numAccesses) && !work_done) {
            if (pDataAccessRequests->GetCount() + reservedCount < pDataAccessRequests->GetCapacity()) {
                int16_t request_index = pTheseCaches[kInstructionCache]->AddAccessRequest(accesses->pInstructionAccesses[i], localCycleCounter);
                if (request_index != -1) {
                    reservedCount++;
                    work_done = true;
                    outstanding_requests[kInstructionCache][request_index] = i;
                    ++i;
                    // Periodically sync the index for use by progress tracker
                    if (i % Simulator::kProgressTrackerSyncPeriod == 0) {
                        pSimulator->pAccessIndices[pTheseCaches[kDataCache]->threadId_] = i;
                    }
                    isOutstandingRequest = true;
                }
            }
        }

        for (auto j = 0; j < kNumberOfCacheTypes; j++) {
#if (CONSOLE_PRINT == 1)
            if (j == kDataCache) {
                printf("Data Cache\n");
            } else if (j == kInstructionCache) {
                printf("Instruction Cache\n");
            }
#endif
            num_completed_requests[j] = pTheseCaches[j]->ProcessCache(localCycleCounter, completed_requests[j]);
            work_done |= pTheseCaches[j]->GetWasWorkDoneThisCycle();
        }

        for (uint64_t j = 0; j < num_completed_requests[kDataCache]; j++) {
            assert(outstanding_requests[kDataCache][completed_requests[kDataCache][j]] == Simulator::kDataAccessRequest);
            outstanding_requests[kDataCache][completed_requests[kDataCache][j]] = Simulator::kInvalidRequestIndex;
        }

        for (uint64_t j = 0; j < num_completed_requests[kInstructionCache]; j++) {
            work_done = true;
            // Clear out outstanding requests
            assert(outstanding_requests[kInstructionCache][completed_requests[kInstructionCache][j]] != Simulator::kDataAccessRequest);

            uint64_t complete_request_index = outstanding_requests[kInstructionCache][completed_requests[kInstructionCache][j]];
            outstanding_requests[kInstructionCache][completed_requests[kInstructionCache][j]] = Simulator::kInvalidRequestIndex;
            // add data access request to queue
            assert(reservedCount);
            reservedCount--;
            DoubleListElement *pElement = pFreeAccessRequests->PopElement();
            assert(pElement);
            pElement->poolIndex_ = complete_request_index;
            pDataAccessRequests->AddElementToTail(pElement);

            isOutstandingRequest = true;
        }
        if (work_done) {
            localCycleCounter++;
        } else {
            uint64_t earliestNextUsefulCycle = UINT64_MAX;
            for (auto j = 0; j < kNumberOfCacheTypes; j++) {
                uint64_t nextUsefulCycle = pTheseCaches[j]->CalculateEarliestNextUsefulCycle();
                earliestNextUsefulCycle = nextUsefulCycle < earliestNextUsefulCycle ? nextUsefulCycle : earliestNextUsefulCycle;
            }
            assert(earliestNextUsefulCycle > localCycleCounter);
            if (earliestNextUsefulCycle < UINT64_MAX) {
#if (CONSOLE_PRINT == 1)
                printf("Skipping to earliest next useful cycle = %" PRIu64 "\n", earliestNextUsefulCycle);
#endif
                localCycleCounter = earliestNextUsefulCycle;
            } else {
                localCycleCounter++;
            }
        }
        if (!isOutstandingRequest) {
            for (auto cacheType = 0; cacheType < kNumberOfCacheTypes; cacheType++) {
                for (uint64_t requestIndex = 0; requestIndex < RequestManager::kMaxNumberOfRequests; requestIndex++) {
                    if (outstanding_requests[cacheType][requestIndex] != Simulator::kInvalidRequestIndex) {
                        isOutstandingRequest = true;
                        break;
                    }
                }
            }
        }
    } while (isOutstandingRequest || i < numAccesses);
    CODE_FOR_ASSERT(Statistics stats = pTheseCaches[kDataCache]->GetStats());
    assert(stats.readHits + stats.readMisses + stats.writeHits + stats.writeMisses == numAccesses);
    pSimulator->pAccessIndices[pTheseCaches[kDataCache]->threadId_] = i;
    cycleCounter[configIndex] = localCycleCounter;
    Multithreading::Lock(&pSimulator->lock_);
#if (SIM_TRACE == 1)
    gSimTracer->WriteThreadBuffer(pTheseCaches[kDataCache]);
#endif
    pSimulator->DecrementConfigsToTest();
    pSimulator->DecrementNumThreadsOutstanding();
    // Mark thread as not in use
    pSimulator->GetThreadsOutstanding()[pTheseCaches[kDataCache]->threadId_] = Simulator::kInvalidThreadId;
    Multithreading::Unlock(&pSimulator->lock_);
    pTheseCaches[kDataCache]->FreeMemory();
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
        pCaches_[i].pCaches[kDataCache]->SetThreadId(threadId);
        pCaches_[i].pCaches[kInstructionCache]->SetThreadId(threadId);
        pContexts[i].caches = &pCaches_[i];
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
    static Configuration configs[kMaxNumberOfCacheLevels];
    for (uint64_t blockSize = Max(minBlockSize, gTestParams.minBlockSize[cacheLevel]); blockSize <= gTestParams.maxBlockSize[cacheLevel]; blockSize <<= 1) {
        for (uint64_t cacheSize = Max(minCacheSize, blockSize); cacheSize <= gTestParams.maxCacheSize[cacheLevel]; cacheSize <<= 1) {
            for (uint8_t blocksPerSet = gTestParams.minBlocksPerSet[cacheLevel]; blocksPerSet <= gTestParams.maxBlocksPerSet[cacheLevel]; blocksPerSet <<= 1) {
                configs[cacheLevel].blockSize = blockSize;
                configs[cacheLevel].cacheSize = cacheSize;
                configs[cacheLevel].associativity = blocksPerSet;
                if (Cache::IsCacheConfigValid(configs[cacheLevel])) {
                    if (cacheLevel < gTestParams.numberOfCacheLevels - 1) {
                        SetupCaches(static_cast<CacheLevel>(cacheLevel + 1), blockSize, gTestParams.minCacheSize[cacheLevel + 1]);
                    } else {
                        pCaches_[threadNumber].pCaches[kDataCache] = new Cache(nullptr, kL1, gTestParams.numberOfCacheLevels, configs);
                        Configuration instructionCacheConfig = Configuration(65536, 1024, 2);
                        pCaches_[threadNumber].pCaches[kInstructionCache] = new Cache(nullptr, kL1, 1, &instructionCacheConfig);
                        threadNumber++;
                    }
                }
            }
        }
    }
}

void Simulator::CalculateNumValidConfigs (uint64_t& pNumConfigs, uint8_t cacheLevel, uint64_t minBlockSize, uint64_t minCacheSize) {
    for (uint64_t blockSize = Max(minBlockSize, gTestParams.minBlockSize[cacheLevel]); blockSize <= gTestParams.maxBlockSize[cacheLevel]; blockSize <<= 1) {
        for (uint64_t cacheSize = Max(minCacheSize, blockSize); cacheSize <= gTestParams.maxCacheSize[cacheLevel]; cacheSize <<= 1) {
            for (uint8_t blocksPerSet = gTestParams.minBlocksPerSet[cacheLevel]; blocksPerSet <= gTestParams.maxBlocksPerSet[cacheLevel]; blocksPerSet <<= 1) {
                Configuration config = Configuration(cacheSize, blockSize, blocksPerSet);
                if (Cache::IsCacheConfigValid(config)) {
                    if (cacheLevel < gTestParams.numberOfCacheLevels - 1) {
                        CalculateNumValidConfigs(pNumConfigs, cacheLevel + 1, blockSize, gTestParams.minCacheSize[cacheLevel + 1]);
                    } else {
                        pNumConfigs++;
                    }
                }
            }
        }
    }
}
