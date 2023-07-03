#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "list.h"
#include "Cache.h"
#include "SimTracer.h"
#include "debug.h"
#include "Memory.h"

#if (SIM_TRACE == 1)
extern SimTracer *g_SimTracer;
#else
extern SimTracer dummySimTracer;
extern SimTracer *g_SimTracer;
#endif

#if (CONSOLE_PRINT == 1)
#define DEBUG_TRACE printf
#else
#define DEBUG_TRACE(...)
#endif

// =====================================
//          Public Functions
// =====================================

Cache::Cache (Cache *pUpperCache, CacheLevel pCacheLevel, uint8_t pNumCacheLevels, Configuration *pCacheConfigs, uint64_t pConfigIndex) {
    Configuration cacheConfig = pCacheConfigs[pCacheLevel];
    upperCache_ = pUpperCache;
    cacheLevel_ = pCacheLevel;
    config_.cacheSize = cacheConfig.cacheSize;
    config_.blockSize = cacheConfig.blockSize;
    blockSizeBits_ = 0;
    config_index_ = pConfigIndex;
    uint64_t tmp = config_.blockSize;
    for (; (tmp & 1) == 0; tmp >>= 1) {
        blockSizeBits_++;
    }
    assert_release((tmp == 1) && "Block size must be a power of 2!");
    assert_release((config_.cacheSize % config_.blockSize == 0) && "Block size must be a factor of cache size!");
    uint64_t numBlocks = config_.cacheSize / config_.blockSize;
    if (numBlocks < cacheConfig.associativity) {
        assert(false);
    }
    config_.associativity = cacheConfig.associativity;
    assert_release(numBlocks % config_.associativity == 0 && "Number of blocks must divide evenly with associativity");
    numSets_ = numBlocks / config_.associativity;
    tmp = numSets_;
    for (; (tmp & 1) == 0; tmp >>= 1)
        ;
    assert_release(tmp == 1 && "Number of sets must be a power of 2");
    blockAddressToSetIndexMask_ = numSets_ - 1;
    earliestNextUsefulCycle_ = UINT64_MAX;
    if (pCacheLevel < pNumCacheLevels - 1) {
        lowerCache_ = new Cache(this, static_cast<CacheLevel>(pCacheLevel + 1), pNumCacheLevels, pCacheConfigs, pConfigIndex);
        lowerCache_->SetUpperCache(static_cast<Memory*>(this));
    } else {
        lowerCache_ = new Memory(this);
    }
    Memory *mainMemory = this;
    for (; mainMemory->GetCacheLevel() != kMainMemory; mainMemory = mainMemory->GetLowerCache())
        ;
    mainMemory_ = mainMemory;
}

Cache::~Cache () {
    if (lowerCache_) {
        delete lowerCache_;
    }
}

void Cache::AllocateMemory () {
    sets_ = new Set[numSets_];
    for (uint64_t i = 0; i < numSets_; i++) {
        sets_[i].ways = new Block[config_.associativity];
        sets_[i].lruList = new uint8_t[config_.associativity];
        for (uint64_t j = 0; j < config_.associativity; j++) {
            sets_[i].lruList[j] = (uint8_t) j;
        }
    }
    requestManager_ = new RequestManager(cacheLevel_);
    if (lowerCache_->GetCacheLevel() != kMainMemory) {
        static_cast<Cache*>(lowerCache_)->AllocateMemory();
    } else {
        lowerCache_->AllocateMemory();
    }
}

void Cache::SetThreadId (uint64_t pThreadId) {
    thread_id_ = pThreadId;
    if (cacheLevel_ != kMainMemory) {
        static_cast<Cache*>(lowerCache_)->SetThreadId(pThreadId);
    }
}

bool Cache::IsCacheConfigValid (Configuration config) {
    assert_release((config.cacheSize % config.blockSize == 0) && "Block size must be a factor of cache size!");
    uint64_t numBlocks = config.cacheSize / config.blockSize;
    return numBlocks >= config.associativity;
}

void Cache::FreeMemory () {
    assert(sets_);
    for (uint64_t i = 0; i < numSets_; i++) {
        assert(sets_[i].ways);
        delete[] sets_[i].ways;
        delete[] sets_[i].lruList;
    }
    delete[] sets_;
    delete requestManager_;
    if (lowerCache_->GetCacheLevel() != kMainMemory) {
        static_cast<Cache*>(lowerCache_)->FreeMemory();
    } else {
        lowerCache_->FreeMemory();
    }
}

uint64_t Cache::ProcessCache (uint64_t pCycle, int16_t *pCompletedRequests) {
    return mainMemory_->InternalProcessCache(pCycle, pCompletedRequests);
}

// =====================================
//          Private Functions
// =====================================

void Cache::updateLRUList (uint64_t pSetIndex, uint8_t mru_index) {
    if (config_.associativity == 1) {
        return;
    }
    uint8_t *lruList = sets_[pSetIndex].lruList;
    uint8_t prev_val = mru_index;
    // find MRU index in the lruList
    for (uint8_t i = 0; i < config_.associativity; i++) {
        uint8_t tmp = lruList[i];
        lruList[i] = prev_val;
        if (tmp == mru_index) {
            break;
        }
        prev_val = tmp;
    }
    g_SimTracer->Print(SIM_TRACE__LRU_UPDATE, static_cast<Memory*>(this), (uint32_t) pSetIndex, lruList[0], lruList[config_.associativity - 1]);
}

int16_t Cache::evictBlock (uint64_t pSetIndex, uint64_t pBlockAddress) {
    int16_t lru_block_index = sets_[pSetIndex].lruList[config_.associativity - 1];
    if (!sets_[pSetIndex].ways[lru_block_index].valid) {
        DEBUG_TRACE("Cache[%hhu] not evicting invalid block from set %lu\n", cacheLevel_, pSetIndex);
        return lru_block_index;
    }
    uint64_t old_block_addr = sets_[pSetIndex].ways[lru_block_index].blockAddress;
    Instruction lower_cache_access = Instruction(old_block_addr << blockSizeBits_, READ);
    if (sets_[pSetIndex].ways[lru_block_index].dirty) {
        lower_cache_access.rw = WRITE;
        ++stats_.writebacks;
        sets_[pSetIndex].ways[lru_block_index].dirty = false;
    }
    if (lowerCache_->AddAccessRequest(lower_cache_access, cycle_) == -1) {
        g_SimTracer->Print(SIM_TRACE__EVICT_FAILED, static_cast<Memory*>(this));
        DEBUG_TRACE("Cache[%hhu] could not make request to lower cache in evictBlock, returning\n", cacheLevel_);
        return -1;
    }
    sets_[pSetIndex].ways[lru_block_index].valid = false;
    return lru_block_index;
}

bool Cache::findBlockInSet (uint64_t pSetIndex, uint64_t pBlockAddress, uint8_t& pBlockIndex) {
    for (uint64_t i = 0; i < config_.associativity; i++) {
        if (sets_[pSetIndex].ways[i].valid && (sets_[pSetIndex].ways[i].blockAddress == pBlockAddress)) {
            pBlockIndex = i;
            updateLRUList(pSetIndex, i);
            return true;
        }
    }
    return false;
}

int16_t Cache::requestBlock (uint64_t pSetIndex, uint64_t pBlockAddress) {
    int16_t blockIndex = evictBlock(pSetIndex, pBlockAddress);
    if (blockIndex == -1) {
        return -1;
    }
    g_SimTracer->Print(SIM_TRACE__EVICT, static_cast<Memory*>(this), pSetIndex, blockIndex);
    Instruction read_request_to_lower_cache = Instruction(pBlockAddress << blockSizeBits_, READ);
    if (lowerCache_->AddAccessRequest(read_request_to_lower_cache, cycle_) == -1) {
        g_SimTracer->Print(SIM_TRACE__REQUEST_FAILED, static_cast<Memory*>(this), NULL);
        DEBUG_TRACE("Cache[%hhu] could not make request to lower cache in requestBlock, returning\n", cacheLevel_);
        return -1;
    }
    sets_[pSetIndex].ways[blockIndex].blockAddress = pBlockAddress;
    sets_[pSetIndex].ways[blockIndex].valid = true;
    assert(sets_[pSetIndex].ways[blockIndex].dirty == false);
    return blockIndex;
}

Status Cache::handleAccess (Request *request) {
    if (cycle_ < request->cycle_to_call_back) {
        DEBUG_TRACE("%lu/%lu cycles for this operation in cacheLevel=%hhu\n", cycle_ - request->cycle, kAccessTimeInCycles[cacheLevel_], cacheLevel_);
        if (earliestNextUsefulCycle_ > request->cycle_to_call_back) {
            DEBUG_TRACE("Cache[%hhu] next useful cycle set to %lu\n", cacheLevel_, request->cycle_to_call_back);
            earliestNextUsefulCycle_ = request->cycle_to_call_back;
        }
        return kWaiting;
    }
    earliestNextUsefulCycle_ = UINT64_MAX;

    Instruction access = request->instruction;
    uint64_t blockAddress = addressToBlockAddress(access.ptr);
    uint64_t setIndex = addressToSetIndex(access.ptr);
    if (sets_[setIndex].busy) {
        DEBUG_TRACE("Cache[%hhu] set %lu is busy\n", cacheLevel_, setIndex);
        return kBusy;
    }
    wasWorkDoneThisCycle_ = true;
    uint8_t blockIndex;
    bool hit = findBlockInSet(setIndex, blockAddress, blockIndex);
    if (hit) {
        g_SimTracer->Print(SIM_TRACE__HIT, static_cast<Memory*>(this),
            requestManager_->GetPoolIndex(request), blockAddress>>32, blockAddress & UINT32_MAX, setIndex);
        if (access.rw == READ) {
            if (request->first_attempt) {
                ++stats_.readHits;
            }
        } else {
            if (request->first_attempt) {
                ++stats_.writeHits;
            }
            sets_[setIndex].ways[blockIndex].dirty = true;
        }
    } else {
        g_SimTracer->Print(SIM_TRACE__MISS, static_cast<Memory*>(this), requestManager_->GetPoolIndex(request), setIndex);
        request->first_attempt = false;
        int16_t requested_block = requestBlock(setIndex, blockAddress);
        if (requested_block == -1) {
            return kMiss;
        }
        blockIndex = (uint8_t) requested_block;
        sets_[setIndex].busy = true;
        DEBUG_TRACE("Cache[%hhu] set %lu marked as busy due to miss\n", cacheLevel_, setIndex);
        if (access.rw == READ) {
            ++stats_.readMisses;
        } else {
            ++stats_.writeMisses;
            sets_[setIndex].ways[blockIndex].dirty = true;
        }
    }
    return hit ? kHit : kMiss;
}

void Cache::ResetCacheSetBusy(uint64_t pSetIndex) {
    sets_[pSetIndex].busy = false;
}

uint64_t Cache::InternalProcessCache (uint64_t cycle, int16_t *completed_requests) {
    uint64_t num_requests_completed = 0;
    wasWorkDoneThisCycle_ = false;
    cycle_ = cycle;
    if (lowerCache_->GetWasWorkDoneThisCycle()) {
        for_each_in_double_list(requestManager_->GetBusyRequests()) {
            DEBUG_TRACE("Cache[%hhu] trying request %lu from busy requests list, address=0x%012lx\n", cacheLevel_, pool_index, requestManager_->GetRequestAtIndex(pool_index)->instruction.ptr);
            Status status = handleAccess(requestManager_->GetRequestAtIndex(pool_index));
            if (status == kHit) {
                DEBUG_TRACE("Cache[%hhu] hit, set=%lu\n", cacheLevel_, addressToSetIndex(requestManager_->GetRequestAtIndex(pool_index)->instruction.ptr));
                if (upperCache_) {
                    Cache *upperCache = static_cast<Cache*>(upperCache_);
                    uint64_t setIndex = upperCache->addressToSetIndex(requestManager_->GetRequestAtIndex(pool_index)->instruction.ptr);
                    DEBUG_TRACE("Cache[%hhu] marking set %lu as no longer busy\n", (uint8_t)(cacheLevel_ - 1), setIndex);
                    static_cast<Cache*>(upperCache_)->ResetCacheSetBusy(setIndex);
                }
                if (cacheLevel_ == kL1) {
                    completed_requests[num_requests_completed++] = pool_index;
                }
                requestManager_->RemoveRequestFromBusyList(element_i);
                requestManager_->PushRequestToFreeList(element_i);
            }
        }
    } else {
        if (lowerCache_ && requestManager_->PeekHeadOfBusyList()) {
            DEBUG_TRACE("Cache[%hhu] no work was done in lower cache, not checking busy list\n", cacheLevel_);
        }
    }
    for_each_in_double_list(requestManager_->GetWaitingRequests()) {
        DEBUG_TRACE("Cache[%hhu] trying request %lu from waiting list, address=0x%012lx\n", cacheLevel_, pool_index, requestManager_->GetRequestAtIndex(pool_index)->instruction.ptr);
        Status status = handleAccess(requestManager_->GetRequestAtIndex(pool_index));
        switch (status) {
        case kHit:
            DEBUG_TRACE("Cache[%hhu] hit, set=%lu\n", cacheLevel_, addressToSetIndex(requestManager_->GetRequestAtIndex(pool_index)->instruction.ptr));
            if (upperCache_) {
                Cache *upperCache = static_cast<Cache*>(upperCache_);
                uint64_t setIndex = upperCache->addressToSetIndex(requestManager_->GetRequestAtIndex(pool_index)->instruction.ptr);
                DEBUG_TRACE("Cache[%hhu] marking set %lu as no longer busy\n", (uint8_t)(cacheLevel_ - 1), setIndex);
                upperCache->sets_[setIndex].busy = false;
            }
            if (cacheLevel_ == kL1) {
                completed_requests[num_requests_completed++] = pool_index;
            }
            requestManager_->RemoveRequestFromWaitingList(element_i);
            requestManager_->PushRequestToFreeList(element_i);
            break;
        case kMiss:
        case kBusy:
            requestManager_->RemoveRequestFromWaitingList(element_i);
            requestManager_->AddRequestToBusyList(element_i);
            break;
        case kWaiting:
            DEBUG_TRACE("Cache[%hhu] request %lu is still waiting, breaking out of loop\n", cacheLevel_, pool_index);
            goto out_of_loop; // Break out of for loop
            break;
        default:
            assert_release(0);
            break;
        }
    }
out_of_loop:
    DEBUG_TRACE("\n");

    if (upperCache_) {
        Cache *upperCache = static_cast<Cache*>(upperCache_);
        num_requests_completed = upperCache->InternalProcessCache(cycle, completed_requests);
        upperCache->SetWasWorkDoneThisCycle(wasWorkDoneThisCycle_);
    }
    return num_requests_completed;
}
