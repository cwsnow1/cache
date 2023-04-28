#include <stdint.h>
#include <pthread.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "Simulator.h"
#include "IOUtilities.h"
#include "Cache.h"
#include "SimTracer.h"
#include "default_test_params.h"
#include "debug.h"

#if (SIM_TRACE == 1)
SimTracer *g_SimTracer;
FILE *sim_trace_f;
#endif

test_params_t g_test_params;


Simulator::Simulator(const char *pInputFilename) {
    numConfigs_ = 0;

    // Look for test parameters file and generate a default if not found
    IOUtilities::LoadTestParameters();

    // Read in trace file
    uint64_t file_length = 0;
    uint8_t *file_contents = IOUtilities::ReadInFile(pInputFilename, &file_length);
    accesses_ = IOUtilities::ParseBuffer(file_contents, file_length);

    assert(accesses_ != NULL);
    numAccesses_ = file_length / FILE_LINE_LENGTH_IN_BYTES;

    CalculateNumValidConfigs(numConfigs_, 0, g_test_params.min_block_size, g_test_params.min_cache_size);
    printf("Total number of possible configs = %lu\n", numConfigs_);
    if (numConfigs_ < static_cast<uint64_t>(g_test_params.max_num_threads) || (g_test_params.max_num_threads < 0)) {
        g_test_params.max_num_threads = numConfigs_;
    }
    configsToTest_ = numConfigs_;
#if (SIM_TRACE == 1)
    uint64_t sim_buffer_memory_size = g_test_params.max_num_threads * kSimTraceBufferSizeInBytes;
    if (sim_buffer_memory_size > MEMORY_USAGE_LIMIT) {
        const int32_t new_max_num_threads = MEMORY_USAGE_LIMIT / kSimTraceBufferSizeInBytes;
        printf("Sim trace buffer memory is too big for %d threads. Lower thread count to %d\n", g_test_params.max_num_threads, new_max_num_threads);
        g_test_params.max_num_threads = new_max_num_threads;
    }
    g_SimTracer = new SimTracer(SIM_TRACE_FILENAME, numConfigs_);
#endif
    cycleCounter_ = new uint64_t[numConfigs_];
    assert(cycleCounter_);
    memset(cycleCounter_, 0, sizeof(uint64_t) * numConfigs_);
    threads_ = new pthread_t[numConfigs_];
    assert(threads_);
    caches_ = new Cache*[numConfigs_];
    assert(caches_);

    SetupCaches(0, g_test_params.min_block_size, g_test_params.min_cache_size);
}


void* Simulator::TrackProgress(void * pObj) {
    Simulator *simulator = static_cast<Simulator*>(pObj);
    printf("Running... %02.0f%% complete\n", 0.0f);
    while(simulator->configsToTest_) {
        float configs_done = (float) (simulator->numConfigs_ - simulator->configsToTest_);
        float progress_percent = (configs_done / (float) simulator->numConfigs_) * 100.0f;
        printf("\x1b[1A");
        printf("Running... %d threads running, %lu to go. %02.0f%% complete\n", simulator->numThreadsOutstanding_, simulator->configsToTest_, progress_percent);
        sleep(1);
    }
    pthread_exit(NULL);
}

void Simulator::PrintStats(FILE* pTextStream, FILE* pCSVStream) {
    float min_cpi = (float) cycleCounter_[0];
    uint64_t min_i = 0;
    if (pCSVStream) {
        for (int i = 0; i < g_test_params.num_cache_levels; i++) {
            fprintf(pCSVStream, "Cache level, Cache size, Block size, Associativity, Num reads, Read miss rate, Num writes, Write miss rate, Total miss rate,");
        }
        fprintf(pCSVStream, "Main memory reads, Main memory writes, Total number of cycles, CPI\n");
    }
    for (uint64_t i = 0; i < numConfigs_; i++) {
        IOUtilities::PrintStatistics(caches_[i], cycleCounter_[i], pTextStream);
        IOUtilities::PrintStatisticsCSV(caches_[i], cycleCounter_[i], pCSVStream);
        Statistics stats = caches_[i]->GetStats();
        float num_reads =  (float) (stats.read_hits  + stats.read_misses);
        float num_writes = (float) (stats.write_hits + stats.write_misses);
        float cpi = (float) cycleCounter_[i] / (num_reads + num_writes);
        if (cpi < min_cpi) {
            min_cpi = cpi;
            min_i = i;
        }
    }
    if (pCSVStream) {
        fclose(pCSVStream);
    }
    fprintf(pTextStream, "The config with the lowest CPI of %.4f:\n", min_cpi);
    IOUtilities::PrintConfiguration(caches_[min_i], pTextStream);
}

Simulator::~Simulator() {
    for (uint64_t i = 0; i < numConfigs_; i++) {
        delete caches_[i];
    }
    delete[] caches_;
    delete[] cycleCounter_;
    delete[] accesses_;
    delete[] threads_;
#if (SIM_TRACE == 1)
    delete g_SimTracer;
    printf("Wrote sim trace output to %s\n", SIM_TRACE_FILENAME);
#endif
}

void* Simulator::SimCache (void *pSimCacheContext) {
    SimCacheContext *simCacheContext = static_cast<SimCacheContext*>(pSimCacheContext);
    Cache *this_cache = simCacheContext->L1Cache;
    Simulator* simulator = simCacheContext->simulator;
    uint64_t* cycleCounter = simulator->GetCycleCounter();
    Instruction* accesses = simulator->GetAccesses();
    const uint64_t numAccesses = simulator->GetNumAccesses();

    assert(this_cache->GetCacheLevel() == kL1);
    this_cache->AllocateMemory();
    uint64_t config_index = this_cache->config_index_;
    cycleCounter[config_index] = 0;
    bitfield64_t oustanding_requests = 0;
    int16_t completed_requests[kMaxNumberOfRequests] = { 0 };
    static_assert(kMaxNumberOfRequests <= 64,"Too many requests to store requests in this bitfield");
    uint64_t i = 0;
    bool work_done = false;
    do {
#if (CONSOLE_PRINT == 1)
        printf("====================\nTICK %010lu\n====================\n", cycleCounter[config_index]);
        char c;
        scanf("%c", &c);
#endif
        work_done = false;
        if (i < numAccesses) {
            int16_t request_index = this_cache->AddAccessRequest(accesses[i], cycleCounter[config_index]);
            if (request_index != -1) {
                work_done = true;
                ++i;
                Simulator::SetBit(oustanding_requests, request_index);
            }
        }
        uint64_t num_completed_requests = this_cache->ProcessCache(cycleCounter[config_index], completed_requests);
        work_done |= this_cache->WasWorkDoneThisCycle();
        for (uint64_t j = 0; j < num_completed_requests; j++) {
            work_done = true;
            Simulator::ResetBit(oustanding_requests, completed_requests[j]);
        }
        if (work_done) {
            cycleCounter[config_index]++;
        } else {
            uint64_t earliest_next_useful_cycle = this_cache->GetEarliestNextUsefulCycle();
            assert(earliest_next_useful_cycle > cycleCounter[config_index]);
            if (earliest_next_useful_cycle < UINT64_MAX) {
#if (CONSOLE_PRINT == 1)
                printf("Skipping to earliest next useful cycle = %lu\n", earliest_next_useful_cycle);
#endif
                cycleCounter[config_index] = earliest_next_useful_cycle;
            } else {
                cycleCounter[config_index]++;
            }
        }
    } while (oustanding_requests || i < numAccesses);
    pthread_mutex_lock(&simulator->lock_);
#if (SIM_TRACE == 1)
    g_SimTracer->WriteThreadBuffer(this_cache);
#endif
    simulator->DecrementConfigsToTest();
    simulator->DecrementNumThreadsOutstanding();
    assert(simulator->GetThreadsOutstanding()[this_cache->thread_id_] == pthread_self());
    // Mark thread as not in use
    simulator->GetThreadsOutstanding()[this_cache->thread_id_] = Simulator::INVALID_THREAD_ID;
    pthread_mutex_unlock(&simulator->lock_);
    this_cache->FreeMemory();
    pthread_exit(NULL);
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
    if (pthread_mutex_init(&lock_, NULL) != 0){
        fprintf(stderr, "Mutex lock init failed\n");
        exit(1);
    }
#if (CONSOLE_PRINT == 0)
    pthread_t progress_thread;
    pthread_create(&progress_thread, NULL, Simulator::TrackProgress, this);
#endif
    uint64_t thread_id = 0;

    // internal thread_id to pthread thread_id mapping used to track which threads are active
    threadsOutstanding_ = new pthread_t[g_test_params.max_num_threads];
    memset(threadsOutstanding_, (int) INVALID_THREAD_ID, sizeof(pthread_t) * g_test_params.max_num_threads);
    SimCacheContext *contexts = new SimCacheContext[numConfigs_];
    for (uint64_t i = 0; i < numConfigs_; i++) {
        while (numThreadsOutstanding_ == g_test_params.max_num_threads)
            ;
        pthread_mutex_lock(&lock_);
        numThreadsOutstanding_++;
        // Search for a thread_id that is not in use
        for (thread_id = 0; thread_id < static_cast<uint64_t>(g_test_params.max_num_threads); thread_id++) {
            if (threadsOutstanding_[thread_id] == INVALID_THREAD_ID) {
                break;
            }
        }
        caches_[i]->SetThreadId(thread_id);
        contexts[i].L1Cache = caches_[i];
        contexts[i].simulator = this;
        if (pthread_create(&threads_[i], NULL, Simulator::SimCache, static_cast<void*>(&contexts[i]))) {
            fprintf(stderr, "Error in creating thread %lu\n", i);
        }
        threadsOutstanding_[thread_id] = threads_[i];
        pthread_mutex_unlock(&lock_);
    }
    for (uint64_t i = 0; i < numConfigs_; i++) {
        pthread_join(threads_[i], NULL);
    }
    delete[] threadsOutstanding_;
#if (CONSOLE_PRINT == 0)
    pthread_join(progress_thread, NULL);
#endif
    assert(numThreadsOutstanding_ == 0);
}

void Simulator::SetupCaches (uint8_t pCacheLevel, uint64_t pMinBlockSize, uint64_t pMinCacheSize) {
    static uint64_t thread_num = 0;
    static Configuration configs[MAX_NUM_CACHE_LEVELS];
    for (uint64_t block_size = pMinBlockSize; block_size <= g_test_params.max_block_size; block_size <<= 1) {
        for (uint64_t cache_size = Max(pMinCacheSize, block_size); cache_size <= (g_test_params.max_cache_size * (1 << pCacheLevel)); cache_size <<= 1) {
            for (uint8_t blocks_per_set = g_test_params.min_blocks_per_set; blocks_per_set <= g_test_params.max_blocks_per_set; blocks_per_set <<= 1) {
                configs[pCacheLevel].block_size = block_size;
                configs[pCacheLevel].cache_size = cache_size;
                configs[pCacheLevel].associativity = blocks_per_set;
                if (Cache::IsCacheConfigValid(configs[pCacheLevel])) {
                    if (pCacheLevel < g_test_params.num_cache_levels - 1) {
                        SetupCaches(pCacheLevel + 1, block_size, cache_size * 2);
                    } else {
                        caches_[thread_num] = new Cache(NULL, kL1, g_test_params.num_cache_levels, configs, thread_num);
                        thread_num++;
                    }
                }
            }
        }
    }
}

void Simulator::CalculateNumValidConfigs (uint64_t& pNumConfigs, uint8_t cache_level, uint64_t min_block_size, uint64_t min_cache_size) {
    for (uint64_t block_size = min_block_size; block_size <= g_test_params.max_block_size; block_size <<= 1) {
        for (uint64_t cache_size = Max(min_cache_size, block_size); cache_size <= g_test_params.max_cache_size; cache_size <<= 1) {
            for (uint8_t blocks_per_set = g_test_params.min_blocks_per_set; blocks_per_set <= g_test_params.max_blocks_per_set; blocks_per_set <<= 1) {
                Configuration config = Configuration(cache_size, block_size, blocks_per_set);
                if (Cache::IsCacheConfigValid(config)) {
                    if (cache_level < g_test_params.num_cache_levels - 1) {
                        CalculateNumValidConfigs(pNumConfigs, cache_level + 1, block_size, cache_size);
                    } else {
                        pNumConfigs++;
                    }
                }
            }
        }
    }
}
